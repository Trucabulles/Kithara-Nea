#include "Core.h"
#include "../common/CpuThreads.h"
#define ORT_API_MANUAL_INIT
#include "onnxruntime_cxx_api.h"
#include <complex>
namespace kn {
Document analyseHarmony(const Document &original, Theory &theory, Cancel cancel, Progress progress) {
    checkCancel(cancel);
    TempFolder temporary;
    auto folder = resource("lv_chordia_native");
    auto manifest = readJson(folder.getChildFile("manifest.json"));
    static std::mutex checkMutex;
    static std::set<String> verified;
    {
        std::lock_guard<std::mutex> guard(checkMutex);
        if (!verified.count(folder.getFullPathName())) {
            for (auto &file : manifest.at("files")) {
                checkCancel(cancel);
                auto path = folder.getChildFile(text(file, "path").replaceCharacter('\\', '/'));
                require(path.isAChildOf(folder), "Ressource native invalide");
                checkFile(path, text(file, "sha256"));
            }
            verified.insert(folder.getFullPathName());
        }
    }
    auto wav = temporary.folder.getChildFile("source.wav"),
         output = temporary.folder.getChildFile("chords.json");
    if (progress)
        progress(.02, juce::String::fromUTF8("Préparation de l’audio"));
    decodeAudio(original.audio(), wav, cancel);
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> originalReader(
        wavFormat.createReaderFor(new juce::FileInputStream(wav), true));
    require(originalReader != nullptr, "Audio decode invalide");
    double analysedDuration = originalReader->lengthInSamples / originalReader->sampleRate;
    if (progress)
        progress(.08, "Analyse spectrale");
    runProcess(
        {folder.getChildFile("kithara-lv-native.exe").getFullPathName(), "--audio", wav.getFullPathName(),
         output.getFullPathName(), folder.getChildFile("assets").getFullPathName(),
         folder.getChildFile("models").getFullPathName()},
        cancel, [&](double, String log) {
            if (progress)
                progress(.4, log.contains("Network") ? "Reconnaissance des accords" : "Analyse spectrale");
        });
    auto raw = readJson(output);
    Document result = original;
    result.chords() = J::array();
    std::map<std::string, std::string> qualities = {
        {"maj", ""},      {"min", "m"},         {"maj7", "M7"},    {"maj9", "M9"}, {"min7", "m7"},
        {"min9", "m9"},   {"7", "7"},           {"9", "9"},        {"11", "11"},   {"13", "13"},
        {"dim", "dim"},   {"dim7", "dim7"},     {"hdim7", "m7b5"}, {"aug", "aug"}, {"sus2", "sus2"},
        {"sus4", "sus4"}, {"sus4(b7)", "7sus4"}};
    double previous = 0;
    for (auto &row : raw) {
        checkCancel(cancel);
        double a = row.at("start_time"), b = row.at("end_time");
        auto chord = text(row, "chord");
        require(std::isfinite(a) && std::isfinite(b) && a >= previous - 1e-6 && b >= a,
                "Chronologie harmonique invalide");
        previous = b;
        a = std::clamp(a, 0., analysedDuration);
        b = std::clamp(b, a, analysedDuration);
        if (b <= a)
            continue;
        String name = "N", bass;
        if (chord != "N") {
            std::smatch match;
            auto value = utf(chord);
            require(std::regex_match(value, match, std::regex("([A-G](?:#|b)?):([^/]+)(?:/([b#]?[0-9]+))?")),
                    "Accord non reconnu : " + chord);
            require(qualities.count(match[2]), juce::String::fromUTF8("Qualité harmonique non reconnue"));
            name = str(match[1].str() + qualities.at(match[2]));
            if (match[3].matched) {
                auto note = (pitch(str(match[1])) + semitone(match[3])) % 12;
                if (note != pitch(str(match[1])))
                    bass = midiName(note + 48).dropLastCharacters(1);
            }
        }
        result.chords().push_back({{"start", a},
                                   {"end", b},
                                   {"name", utf(name)},
                                   {"bass", utf(bass)},
                                   {"confidence", 0.},
                                   {"notes", theory.notes(name, bass)},
                                   {"source", "LV-Chordia"}});
    }
    if (!result.chords().empty()) {
        result.chords().front()["start"] = 0.;
        result.chords().back()["end"] = analysedDuration;
    }
    result.j["model"] = "LVChordia";
    result.j["key"] = utf(theory.key(result));
    result.j["provenance"] = "Analyse harmonique native C++/ONNX.";
    if (progress)
        progress(1, "Accords disponibles");
    return result;
}
static void fft(std::vector<std::complex<double>> &x) {
    auto n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (size_t length = 2; length <= n; length <<= 1) {
        std::complex<double> step = std::polar(1., -2 * juce::MathConstants<double>::pi / length);
        for (size_t start = 0; start < n; start += length) {
            std::complex<double> w = 1;
            for (size_t j = 0; j < length / 2; ++j) {
                auto a = x[start + j], b = x[start + j + length / 2] * w;
                x[start + j] = a + b;
                x[start + j + length / 2] = a - b;
                w *= step;
            }
        }
    }
}
std::vector<float> beatFeatures(const AudioData &audio, Cancel cancel) {
    const bool official = audio.rate == 22050;
    const int hop = official ? 441 : 320, windowLength = official ? 1024 : 743,
              padding = (1024 - windowLength) / 2;
    int count = (int)(audio.mono.size() / hop) + 1;
    std::array<float, 1024> window{};
    for (int k = 0; k < windowLength; ++k)
        window[k + padding] =
            (float)(.5 - .5 * std::cos(2 * juce::MathConstants<double>::pi * k / windowLength));
    double hz[130];
    for (int i = 0; i < 130; ++i) {
        double m = .45 + i * (49.42757499150438 / 129);
        hz[i] = m < 15 ? m * (200. / 3) : 1000 * std::exp((m - 15) * .06875177742094912);
    }
    std::vector<float> bank(128 * 513);
    for (int m = 0; m < 128; ++m)
        for (int k = 0; k < 513; ++k) {
            double f = k * audio.rate / 1024;
            bank[m * 513 + k] = (float)std::max(
                0., std::min((f - hz[m]) / (hz[m + 1] - hz[m]), (hz[m + 2] - f) / (hz[m + 2] - hz[m + 1])));
        }
    std::vector<float> result((size_t)count * 128), magnitude(513);
    std::vector<std::complex<double>> frame(1024);
    auto reflect = [&](int64_t at) {
        if (audio.mono.size() < 2)
            return audio.mono.empty() ? 0.f : audio.mono[0];
        int64_t period = 2 * ((int64_t)audio.mono.size() - 1);
        at = (at % period + period) % period;
        if (at >= (int64_t)audio.mono.size())
            at = period - at;
        return audio.mono[(size_t)at];
    };
    for (int t = 0; t < count; ++t) {
        if (t % 64 == 0)
            checkCancel(cancel);
        for (int k = 0; k < 1024; ++k)
            frame[k] = (double)(reflect((int64_t)t * hop + k - 512) * window[k]);
        fft(frame);
        for (int k = 0; k < 513; ++k)
            magnitude[k] = (float)(std::abs(frame[k]) / 32);
        for (int m = 0; m < 128; ++m) {
            float sum = 0;
            for (int k = 0; k < 513; ++k)
                sum += magnitude[k] * bank[m * 513 + k];
            result[(size_t)t * 128 + m] = std::log1p(1000.f * sum);
        }
    }
    return result;
}
struct BeatRuntime {
    SessionLibrary library;
    Ort::Env env{nullptr};
    Ort::Session session{nullptr};
    BeatRuntime() : library(resource("lv_chordia_native/onnxruntime.dll")) {
        auto api = library.get<const OrtApiBase *(ORT_API_CALL *)()>("OrtGetApiBase")();
        Ort::InitApi(api->GetApi(ORT_API_VERSION));
        auto provenance = readJson(resource("models/beat_this_official/onnx-provenance.json"));
        auto path = resource("models/beat_this_official/small0.onnx");
        checkFile(path, text(provenance, "sha256"));
        env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "kithara-rhythm");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(analysisCpuThreads());
        options.SetInterOpNumThreads(1);
        session = Ort::Session(env, path.getFullPathName().toWideCharPointer(), options);
    }
};
static std::vector<int> peakIndices(const std::vector<float> &values) {
    std::vector<int> peaks;
    for (int i = 1; i + 1 < (int)values.size(); ++i)
        if (values[i] > values[i - 1]) {
            int end = i;
            while (end + 1 < (int)values.size() && values[end + 1] == values[i])
                ++end;
            if (end + 1 < (int)values.size() && values[end] > values[end + 1]) {
                int centre = (i + end) / 2;
                if (values[centre] >= .35f)
                    peaks.push_back(centre);
            }
            i = end;
        }
    auto priority = peaks;
    std::sort(priority.begin(), priority.end(),
              [&](int a, int b) { return values[a] == values[b] ? a > b : values[a] > values[b]; });
    std::set<int> keep;
    for (int p : priority) {
        auto right = keep.lower_bound(p);
        if (right != keep.end() && *right - p < 10)
            continue;
        if (right != keep.begin() && p - *std::prev(right) < 10)
            continue;
        keep.insert(p);
    }
    std::vector<int> result;
    for (int p : keep) {
        float left = values[p], right = values[p];
        for (int i = p; i >= 0 && values[i] <= values[p]; --i)
            left = std::min(left, values[i]);
        for (int i = p; i < (int)values.size() && values[i] <= values[p]; ++i)
            right = std::min(right, values[i]);
        if (values[p] - std::max(left, right) >= .15f)
            result.push_back(p);
    }
    return result;
}
void analyseBeats(Document &document, const AudioData &audio, Cancel cancel, Progress progress,
                  const File &diagnostic) {
    if (progress)
        progress(0, "Analyse rythmique");
    // Decode original bandwidth for the model frontend; playback/Whisper remain at 16 kHz.
    std::shared_ptr<AudioData> original;
    if (document.audio().existsAsFile())
        original = loadAudio(document.audio(), cancel, 22050);
    const auto &rhythmAudio = original ? *original : audio;
    auto features = beatFeatures(rhythmAudio, cancel);
    int count = (int)(features.size() / 128);
    static std::unique_ptr<BeatRuntime> runtime;
    static std::mutex mutex;
    std::lock_guard<std::mutex> guard(mutex);
    if (!runtime)
        runtime = std::make_unique<BeatRuntime>();
    std::vector<float> beat(count), down(count), weights(count), input(1500 * 128);
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 3> dimensions = {1, 1500, 128};
    const char *inputs[] = {"spectrogram"};
    const char *outputs[] = {"beat", "downbeat"};
    for (int start = -250; start < count; start += 1000) {
        checkCancel(cancel);
        for (int row = 0; row < 1500; ++row) {
            int at = std::clamp(start + row, 0, count - 1);
            std::copy_n(features.data() + (size_t)at * 128, 128, input.data() + (size_t)row * 128);
        }
        auto value =
            Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), dimensions.data(), 3);
        auto result = runtime->session.Run(Ort::RunOptions{nullptr}, inputs, &value, 1, outputs, 2);
        auto a = result[0].GetTensorData<float>(), b = result[1].GetTensorData<float>();
        int lo = std::max(0, start), hi = std::min(count, start + 1500);
        for (int i = lo; i < hi; ++i) {
            int local = i - start;
            float weight = std::min({(local + 1) / 251.f, (1500 - local) / 251.f, 1.f});
            beat[i] += a[local] * weight;
            down[i] += b[local] * weight;
            weights[i] += weight;
        }
        if (progress)
            progress((double)hi / count, "Analyse rythmique");
        if (hi >= count)
            break;
    }
    for (int i = 0; i < count; ++i) {
        beat[i] = 1.f / (1.f + std::exp(-beat[i] / std::max(weights[i], 1e-6f)));
        down[i] = 1.f / (1.f + std::exp(-down[i] / std::max(weights[i], 1e-6f)));
    }
    document.j["beats"] = J::array();
    document.j["downbeats"] = J::array();
    std::vector<double> times;
    for (int p : peakIndices(beat)) {
        double t = p * .02;
        if (t >= audio.seconds())
            continue;
        document.j["beats"].push_back(t);
        times.push_back(t);
        if (down[p] > .35)
            document.j["downbeats"].push_back(t);
    }
    estimateRhythmActivations(document, beat, down, cancel);
    if (diagnostic != File()) {
        writeJson(diagnostic, {{"beatProbability", beat},
                               {"downbeatProbability", down},
                               {"beats", document.j["beats"]},
                               {"downbeats", document.j["downbeats"]},
                               {"bpm", document.j["bpm"]},
                               {"meter", document.j["meter"]},
                               {"sampleRate", rhythmAudio.rate},
                               {"hop", rhythmAudio.rate == 22050 ? 441 : 320},
                               {"window", rhythmAudio.rate == 22050 ? 1024 : 743},
                               {"rhythm_estimate", document.j["rhythm_estimate"]}});
    }
}
void estimateMeter(Document &document) {
    auto &beats = document.j.at("beats");
    auto &down = document.j.at("downbeats");
    std::map<int, int> votes;
    int valid = 0;
    for (size_t i = 1; i < down.size(); ++i) {
        double from = down[i - 1], to = down[i];
        if (to - from < .4 || to - from > 20)
            continue;
        auto index = [&](double t) {
            auto at = std::lower_bound(beats.begin(), beats.end(), t - 1e-5,
                                       [](const J &a, double b) { return a.get<double>() < b; });
            return (int)std::distance(beats.begin(), at);
        };
        int count = index(to) - index(from);
        if (count >= 2 && count <= 12) {
            votes[count]++;
            valid++;
        }
    }
    int count = 0, best = 0;
    for (auto item : votes)
        if (item.second > best) {
            count = item.first;
            best = item.second;
        }
    bool enough = valid >= 4 && best >= 4 && (double)best / valid >= .75 && (count == 3 || count == 4);
    document.j["meter"] = enough ? count : 0;
    document.j["meter_denominator"] = 4;
    document.j["meter_source"] = enough ? "estimated" : "unknown";
    document.j["bpm_source"] = document.j.value("bpm", 0.) > 0 ? "estimated" : "unknown";
}
static void whisperWav(const File &path, const AudioData &audio) {
    juce::FileOutputStream out(path);
    require(out.openedOk(), "Audio temporaire inaccessible");
    uint32_t bytes = (uint32_t)audio.mono.size() * 2;
    out.write("RIFF", 4);
    out.writeInt((int)(bytes + 36));
    out.write("WAVEfmt ", 8);
    out.writeInt(16);
    out.writeShort(1);
    out.writeShort(1);
    out.writeInt(16000);
    out.writeInt(32000);
    out.writeShort(2);
    out.writeShort(16);
    out.write("data", 4);
    out.writeInt((int)bytes);
    for (float f : audio.mono)
        out.writeShort((short)(std::clamp(f, -1.f, 1.f) * 32767));
    out.flush();
}
static const J &experimentalLyricTiming() {
    static const J config = [] {
        auto file = resource("whisper/experimental-timing.json");
        return file.existsAsFile() ? readJson(file) : J::object();
    }();
    return config;
}
J runWhisperCompute(const juce::StringArray &cpuCommand, const File &gpuExecutable, const File &output,
                    String compute, Cancel cancel, const WhisperRunner &providedRunner, double timeout) {
    require(compute == "auto" || compute == "cpu" || compute == "gpu", "Invalid Whisper compute mode");
    auto runner =
        providedRunner ? providedRunner : WhisperRunner([&](const juce::StringArray &args, double timeout) {
            return runProcess(args, cancel, {}, timeout);
        });
    bool fallback = false;
    String reason, deviceName;
    auto invoke = [&](juce::StringArray command, bool gpu) {
        checkCancel(cancel);
        // Never accept a partial result from a failed GPU attempt.
        require(!output.existsAsFile() || output.deleteFile(), "Cannot discard previous Whisper output");
        auto log = runner(command, timeout);
        checkCancel(cancel);
        auto raw = readJson(output);
        require(raw.is_object() && raw.contains("transcription") && raw["transcription"].is_array(),
                "Invalid Whisper output");
        if (gpu)
            require(log.contains("using Vulkan"), "Whisper did not activate the GPU backend");
        return J{{"data", raw},
                 {"backend", gpu ? "gpu" : "cpu"},
                 {"fallback", fallback},
                 {"fallback_reason", utf(reason)},
                 {"device", utf(deviceName)}};
    };
    if (compute != "cpu") {
        try {
            require(gpuExecutable.existsAsFile(), "GPU decoder unavailable");
            if (!providedRunner) {
                auto loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                require(loader != nullptr, "Vulkan loader unavailable");
                FreeLibrary(loader);
            }
            auto probe = runner({gpuExecutable.getFullPathName(), "--kithara-gpu-probe"}, 20);
            const String marker = "KITHARA_GPU_READY=";
            require(probe.contains(marker), "Compatible GPU unavailable");
            auto index = probe.fromFirstOccurrenceOf(marker, false, false)
                             .upToFirstOccurrenceOf("\n", false, false)
                             .trim();
            require(index.containsOnly("0123456789") && index.isNotEmpty(), "Invalid GPU response");
            deviceName = probe.fromFirstOccurrenceOf("KITHARA_GPU_NAME=", false, false)
                             .upToFirstOccurrenceOf("\n", false, false)
                             .trim();
            auto command = cpuCommand;
            command.set(0, gpuExecutable.getFullPathName());
            command.removeString("-ng");
            command.addArray({"-dev", index});
            fallback = true; // Any subsequent failure must be reported as a CPU retry.
            auto result = invoke(command, true);
            result["fallback"] = false;
            return result;
        } catch (const Cancelled &) {
            throw;
        } catch (const std::exception &error) {
            checkCancel(cancel);
            fallback = fallback || compute == "gpu";
            reason = str(error.what()).substring(0, 500);
        }
    }
    auto command = cpuCommand;
    if (!command.contains("-ng"))
        command.add("-ng");
    return invoke(command, false);
}
J transcribe(const AudioData &audio, String model, String language, Cancel cancel, Progress progress,
             String compute) {
    require(model == "small" || model == "medium", juce::String::fromUTF8("Modèle de transcription inconnu"));
    require(audio.mono.size() <= 16000ull * 10800 && !audio.mono.empty(), "Audio invalide");
    auto folder = resource("whisper");
    auto manifest = readJson(folder.getChildFile("manifest-sha256.json"));
    auto weight = "models/ggml-" + model + ".bin";
    static std::set<String> verified;
    for (auto &row : manifest.items()) {
        checkCancel(cancel);
        auto name = str(row.key());
        // The optional GPU executable is verified separately, so a missing or
        // damaged backend cannot prevent the independent CPU decoder from running.
        if (name == "bin/whisper-vulkan.exe")
            continue;
        if (name.startsWith("models/") && name != weight)
            continue;
        auto file = name == weight ? whisperModelFile(model) : folder.getChildFile(name);
        auto key = file.getFullPathName() + ":" + file.getLastModificationTime().toISO8601(true);
        if (!verified.count(key)) {
            checkFile(file, str(row.value().get<std::string>()));
            verified.insert(key);
        }
    }
    if (progress)
        progress(.05, "Transcription des paroles");
    float peak = 0;
    for (float f : audio.mono)
        peak = std::max(peak, std::abs(f));
    if (peak < 1e-5f)
        return {{"lyrics", J::array()}, {"language", utf(language)}};
    TempFolder temp;
    auto wav = temp.folder.getChildFile("audio.wav"), output = temp.folder.getChildFile("paroles");
    whisperWav(wav, audio);
    const bool dtw = juce::SystemStats::getEnvironmentVariable("KITHARA_LYRIC_DTW", {}) == "1" ||
                     experimentalLyricTiming().value("dtw", false);
    auto executable =
        dtw ? File(juce::SystemStats::getEnvironmentVariable(
                  "KITHARA_DTW_EXECUTABLE", folder.getChildFile("bin/whisper-path.exe").getFullPathName()))
            : folder.getChildFile("bin/whisper-cli.exe");
    require(executable.existsAsFile(), "Whisper executable unavailable");
    juce::StringArray command{executable.getFullPathName(),
                              "-m",
                              whisperModelFile(model).getFullPathName(),
                              "-f",
                              wav.getFullPathName(),
                              "-l",
                              language,
                              "-ojf",
                              "-of",
                              output.getFullPathName(),
                              "-pp",
                              "-t",
                              String(analysisCpuThreads())};
    if (dtw)
        command.addArray({"-dtw", model, "-nfa"});
    command.add("-ng");
    auto gpuExecutable = File(juce::SystemStats::getEnvironmentVariable(
        "KITHARA_WHISPER_GPU_EXECUTABLE", folder.getChildFile("bin/whisper-vulkan.exe").getFullPathName()));
    if (compute != "cpu" && manifest.contains("bin/whisper-vulkan.exe")) {
        try {
            const auto key = gpuExecutable.getFullPathName() + ":" +
                             gpuExecutable.getLastModificationTime().toISO8601(true);
            if (!verified.count(key)) {
                checkFile(gpuExecutable, str(manifest.at("bin/whisper-vulkan.exe").get<std::string>()));
                verified.insert(key);
            }
        } catch (const std::exception &) {
            gpuExecutable = File();
        }
    }
    const auto calculation = runWhisperCompute(command, gpuExecutable, output.withFileExtension("json"),
                                               compute, cancel, {}, std::max(300., audio.seconds() * 30));
    auto result = parseWhisperLyrics(calculation.at("data"), audio.seconds(), dtw);
    result["compute_backend"] = calculation.at("backend");
    result["compute_fallback"] = calculation.at("fallback");
    result["compute_device"] = calculation.at("device");
    return result;
}
J parseWhisperLyrics(const J &data, double duration, bool dtw) {
    J cues = J::array();
    for (auto &segment : data.value("transcription", J::array())) {
        double a = segment.at("offsets").at("from").get<double>() / 1000,
               b = std::min(duration, segment.at("offsets").at("to").get<double>() / 1000);
        auto value = text(segment, "text").trim();
        if (!std::isfinite(a) || !std::isfinite(b) || a < 0 || b <= a || value.isEmpty())
            continue;
        J cue = {{"start", a}, {"end", b}, {"text", utf(value)}, {"words", J::array()}};
        std::vector<J> tokens;
        String combined;
        for (auto &t : segment.value("tokens", J::array()))
            if (t.contains("id") && t["id"].is_number_integer() && t["id"].get<int>() >= 0 &&
                t["id"].get<int>() < 50257) {
                tokens.push_back(t);
                combined += text(t, "text");
            }
        if (combined.trim() == value) {
            bool useDtw = dtw && !tokens.empty();
            double preceding = -1;
            for (const auto &token : tokens) {
                const double from = token.value("t_dtw_start", -1.) / 100.,
                             to = token.value("t_dtw_end", -1.) / 100.;
                if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to < from || from < preceding ||
                    to > duration + .021)
                    useDtw = false;
                preceding = to;
            }
            int trim = combined.length() - combined.trimStart().length(), position = -trim;
            struct Span {
                int a, b;
                double from, to;
            };
            std::vector<Span> spans;
            for (auto &t : tokens) {
                auto word = text(t, "text");
                int stop = position + word.length();
                auto offsets = t.value("offsets", J::object());
                double from = useDtw ? t.at("t_dtw_start").get<double>() / 100.
                                     : offsets.value("from", -1000.) / 1000,
                       to = useDtw ? t.at("t_dtw_end").get<double>() / 100.
                                   : offsets.value("to", -1000.) / 1000;
                if (from >= 0 && to >= 0) {
                    from = std::max(useDtw ? 0. : a, from);
                    to = std::min(useDtw ? duration : b, to);
                    if (to >= from)
                        spans.push_back({position, stop, from, to});
                }
                position = stop;
            }
            double previous = useDtw ? 0. : a;
            for (int at = 0; at < value.length();) {
                while (at < value.length() && juce::CharacterFunctions::isWhitespace(value[at]))
                    ++at;
                int end = at;
                while (end < value.length() && !juce::CharacterFunctions::isWhitespace(value[end]))
                    ++end;
                if (end <= at)
                    break;
                double from = 1e10, to = -1;
                for (auto &span : spans)
                    if (span.a < end && span.b > at) {
                        from = std::min(from, span.from);
                        to = std::max(to, span.to);
                    }
                if (to < 0 || from < previous) {
                    cue["words"] = J::array();
                    break;
                }
                cue["words"].push_back(
                    {{"start", from}, {"end", to}, {"text", utf(value.substring(at, end))}, {"offset", at}});
                previous = from;
                at = end;
            }
            if (useDtw && !cue["words"].empty()) {
                cue["start"] = cue["words"].front().at("start");
                cue["end"] = cue["words"].back().at("end");
                cue["timing_source"] = "dtw-path";
            } else if (dtw)
                cue["timing_source"] = "token-fallback";
        }
        cues.push_back(cue);
    }
    if (dtw) {
        // Follow Whisper's long-leading-word safeguard. This is speech-derived and
        // remains experimental on sustained singing; retain its raw boundary for comparison.
        std::vector<double> durations;
        for (const auto &cue : cues)
            if (text(cue, "timing_source") == "dtw-path")
                for (const auto &word : cue.at("words")) {
                    double length = word.at("end").get<double>() - word.at("start").get<double>();
                    if (length > 0)
                        durations.push_back(length);
                }
        if (!durations.empty()) {
            std::sort(durations.begin(), durations.end());
            const size_t middle = durations.size() / 2;
            const double median =
                std::min(.7, durations.size() % 2 ? durations[middle]
                                                  : (durations[middle - 1] + durations[middle]) * .5);
            double previousEnd = 0;
            for (auto &cue : cues) {
                if (text(cue, "timing_source") == "dtw-path" && !cue["words"].empty()) {
                    auto &first = cue["words"].front();
                    const double from = first.at("start"), to = first.at("end");
                    if (median > 0 && to - from > median * 2 && to - previousEnd > median * 4) {
                        first["dtw_raw_start"] = from;
                        first["start"] = to - median * 2;
                        cue["start"] = first["start"];
                    }
                }
                previousEnd = cue.at("end");
            }
        }
    }
    auto detected = data.value("result", J::object()).value("language", "auto");
    return {{"lyrics", cues}, {"language", detected}};
}
double lyricDisplayTime(double seconds) {
    static const int lead = [] {
        const auto configured = juce::SystemStats::getEnvironmentVariable("KITHARA_LYRIC_PREVIEW_MS", {});
        const int value = configured.isNotEmpty()
                              ? configured.getIntValue()
                              : experimentalLyricTiming().value("displayAnticipationMs", 0);
        return value == 30 || value == 60 ? value : 0;
    }();
    return seconds + lead / 1000.;
}
double lyricCueEnd(const J &cue) {
    if (!cue.contains("words") || cue.at("words").empty())
        return cue.at("end").get<double>();
    return cue.at("words").back().at("end").get<double>();
}
int activeLyricCue(const J &lyrics, double seconds) {
    auto find = [&](double startClock) {
        int active = -1;
        for (int i = 0; i < (int)lyrics.size(); ++i) {
            const auto &cue = lyrics[(size_t)i];
            if (cue.at("start").get<double>() <= startClock && seconds < lyricCueEnd(cue) &&
                (active < 0 ||
                 cue.at("start").get<double>() > lyrics[(size_t)active].at("start").get<double>()))
                active = i;
        }
        return active;
    };
    // Never replace a phrase that is still being sung with an anticipated next phrase.
    const int actual = find(seconds);
    return actual >= 0 ? actual : find(lyricDisplayTime(seconds));
}
int activeLyricWord(const J &cue, double seconds) {
    if (!cue.contains("words"))
        return -1;
    const auto &words = cue.at("words");
    int active = -1;
    for (int i = 0; i < (int)words.size(); ++i)
        if (words[(size_t)i].at("start").get<double>() <= lyricDisplayTime(seconds) &&
            seconds < words[(size_t)i].at("end").get<double>())
            active = i;
    return active;
}
} // namespace kn
