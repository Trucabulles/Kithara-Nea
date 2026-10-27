#include "Core.h"
#include <numeric>
namespace kn {
void checkCancel(const Cancel &cancel) {
    if (cancel && cancel())
        throw Cancelled();
}
static String quoteArgument(const String &arg) {
    String out = "\"";
    int slashes = 0;
    for (auto c : arg) {
        if (c == '\\') {
            ++slashes;
            continue;
        }
        if (c == '"') {
            out += String::repeatedString("\\", slashes * 2 + 1);
            out += '"';
        } else {
            out += String::repeatedString("\\", slashes);
            out += String::charToString(c);
        }
        slashes = 0;
    }
    out += String::repeatedString("\\", slashes * 2);
    return out + '"';
}
String runProcess(const juce::StringArray &args, Cancel cancel, Progress progress, double timeout) {
    require(args.size() > 0, "Commande vide");
    checkCancel(cancel);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    require(CreatePipe(&read, &write, &security, 0), juce::String::fromUTF8("Création du canal impossible"));
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write;
    startup.hStdError = write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    juce::StringArray quoted;
    for (auto &a : args)
        quoted.add(quoteArgument(a));
    auto command = quoted.joinIntoString(" ");
    std::wstring mutableCommand(command.toWideCharPointer());
    auto ok = CreateProcessW(
        args[0].toWideCharPointer(), mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        File(args[0]).getParentDirectory().getFullPathName().toWideCharPointer(), &startup, &process);
    CloseHandle(write);
    if (!ok) {
        CloseHandle(read);
        throw std::runtime_error(utf(juce::String::fromUTF8("Démarrage impossible : ")) + utf(args[0]) +
                                 " (" + std::to_string(GetLastError()) + ")");
    }
    String tail;
    auto start = juce::Time::getMillisecondCounterHiRes();
    try {
        bool running = true;
        while (running) {
            checkCancel(cancel);
            require((juce::Time::getMillisecondCounterHiRes() - start) / 1000 < timeout,
                    juce::String::fromUTF8("Le traitement dépasse son délai de calcul"));
            DWORD available = 0;
            while (PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr) && available) {
                char buffer[8193];
                DWORD count = 0;
                require(ReadFile(read, buffer, std::min<DWORD>(available, 8192), &count, nullptr),
                        "Lecture du traitement impossible");
                buffer[count] = 0;
                tail += String::fromUTF8(buffer, (int)count);
                if (tail.length() > 32000)
                    tail = tail.substring(tail.length() - 32000);
                if (progress)
                    progress(-1, tail.getLastCharacters(200));
            }
            running = WaitForSingleObject(process.hProcess, 20) == WAIT_TIMEOUT;
        }
        DWORD code = 1;
        GetExitCodeProcess(process.hProcess, &code);
        require(code == 0, "Traitement interrompu : " + tail.getLastCharacters(2500));
        CloseHandle(read);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return tail;
    } catch (...) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 3000);
        CloseHandle(read);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw;
    }
}
SessionLibrary::SessionLibrary(const File &f) {
    handle = LoadLibraryExW(f.getFullPathName().toWideCharPointer(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(handle != nullptr, juce::String::fromUTF8("Bibliothèque native absente : ") + f.getFileName());
}
SessionLibrary::~SessionLibrary() {
    if (handle)
        FreeLibrary(handle);
}
static double i0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 80; ++k) {
        term *= x * x / (4 * k * k);
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}
std::vector<float> resample(const std::vector<float> &input, int from, int to) {
    if (from == to)
        return input;
    require(from > 0 && to > 0, juce::String::fromUTF8("Fréquence invalide"));
    auto gcd = std::gcd(from, to);
    int up = to / gcd, down = from / gcd, rate = std::max(up, down), half = 10 * rate;
    std::vector<double> hd(2 * half + 1);
    double sum = 0, den = i0(5.);
    for (int k = -half; k <= half; ++k) {
        double sinc = k == 0 ? 1.
                             : std::sin(juce::MathConstants<double>::pi * k / rate) /
                                   (juce::MathConstants<double>::pi * k / rate);
        double a = (double)k / half;
        double h = sinc / rate * i0(5 * std::sqrt(std::max(0., 1 - a * a))) / den;
        hd[k + half] = h;
        sum += h;
    }
    std::vector<float> h(hd.size());
    for (size_t k = 0; k < h.size(); ++k)
        h[k] = (float)(hd[k] / sum) * up;
    size_t size = (input.size() * (uint64_t)up + down - 1) / down;
    std::vector<float> result(size);
    for (size_t k = 0; k < size; ++k) {
        int64_t centre = (int64_t)k * down;
        int64_t first = std::max<int64_t>(0, (centre - half + up - 1) / up),
                last = std::min<int64_t>((int64_t)input.size() - 1, (centre + half) / up);
        float value = 0;
        for (auto n = first; n <= last; ++n)
            value += input[(size_t)n] * h[(size_t)(centre + half - n * up)];
        result[k] = value;
    }
    return result;
}
void decodeAudio(const File &source, const File &target, Cancel cancel) {
    runProcess({resource("bin/ffmpeg.exe").getFullPathName(), "-v", "error", "-nostdin",
                "-protocol_whitelist", "file,pipe", "-i", source.getFullPathName(), "-vn", "-acodec",
                "pcm_f32le", "-t", "10800", "-y", target.getFullPathName()},
               cancel);
}
std::shared_ptr<AudioData> loadAudio(const File &file, Cancel cancel, int targetRate) {
    require(file.existsAsFile(), "Fichier audio introuvable");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    std::unique_ptr<TempFolder> temporary;
    if (!reader) {
        temporary = std::make_unique<TempFolder>();
        auto wav = temporary->folder.getChildFile("decoded.wav");
        runProcess({resource("bin/ffmpeg.exe").getFullPathName(), "-v", "error", "-nostdin",
                    "-protocol_whitelist", "file,pipe", "-i", file.getFullPathName(), "-vn", "-ac", "1",
                    "-ar", String(targetRate), "-acodec", "pcm_f32le", "-t", "10800", "-y",
                    wav.getFullPathName()},
                   cancel);
        reader.reset(formats.createReaderFor(wav));
    }
    require(reader && reader->lengthInSamples > 0 && reader->lengthInSamples <= reader->sampleRate * 10800,
            juce::String::fromUTF8("Audio vide ou durée supérieure à trois heures"));
    std::vector<float> mono((size_t)reader->lengthInSamples);
    juce::AudioBuffer<float> buffer((int)reader->numChannels, 65536);
    for (int64_t start = 0; start < reader->lengthInSamples; start += 65536) {
        checkCancel(cancel);
        int count = (int)std::min<int64_t>(65536, reader->lengthInSamples - start);
        require(reader->read(&buffer, 0, count, start, true, true),
                juce::String::fromUTF8("Décodage audio impossible"));
        for (int i = 0; i < count; ++i) {
            float sum = 0;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                sum += buffer.getSample(ch, i);
            float n = sum / std::max(1, buffer.getNumChannels());
            mono[(size_t)start + i] = std::isfinite(n) ? n : 0;
        }
    }
    auto result = std::make_shared<AudioData>();
    result->mono = resample(mono, (int)reader->sampleRate, targetRate);
    result->rate = targetRate;
    return result;
}
void saveWave(const File &path, const std::vector<float> &audio, double rate, int bits) {
    auto stream = std::make_unique<juce::FileOutputStream>(path);
    stream->setPosition(0);
    stream->truncate();
    require(stream->openedOk(), juce::String::fromUTF8("Écriture WAV impossible"));
    juce::WavAudioFormat format;
    auto options =
        juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(1).withBitsPerSample(bits);
    std::unique_ptr<juce::OutputStream> output = std::move(stream);
    auto writer = format.createWriterFor(output, options);
    require(writer != nullptr, juce::String::fromUTF8("Création WAV impossible"));
    for (size_t start = 0; start < audio.size(); start += 65536) {
        const float *p = audio.data() + start;
        require(writer->writeFromFloatArrays(&p, 1, (int)std::min<size_t>(65536, audio.size() - start)),
                juce::String::fromUTF8("Écriture WAV incomplète"));
    }
}
Synth::Synth() {
    catalog = readJson(resource("independent/sounds/catalog.json"));
    library = std::make_unique<SessionLibrary>(resource("fluidsynth/libfluidsynth-3.dll"));
    settings = library->get<void *(*)()>("new_fluid_settings")();
    require(settings != nullptr, juce::String::fromUTF8("Initialisation du synthétiseur impossible"));
    auto number = library->get<int (*)(void *, const char *, double)>("fluid_settings_setnum");
    number(settings, "synth.sample-rate", rate);
    number(settings, "synth.gain", .5);
    auto integer = library->get<int (*)(void *, const char *, int)>("fluid_settings_setint");
    integer(settings, "synth.reverb.active", 0);
    integer(settings, "synth.chorus.active", 0);
    handle = library->get<void *(*)(void *)>("new_fluid_synth")(settings);
    require(handle != nullptr, juce::String::fromUTF8("Synthétiseur indisponible"));
}
Synth::~Synth() {
    if (handle)
        library->get<void (*)(void *)>("delete_fluid_synth")(handle);
    if (settings)
        library->get<void (*)(void *)>("delete_fluid_settings")(settings);
}
std::vector<float> Synth::chord(std::vector<int> notes, double duration, String timbre) {
    J config;
    for (auto &row : catalog.at("timbres"))
        if (text(row, "id") == timbre)
            config = row;
    require(!config.is_null(), "Timbre inconnu");
    if (!banks.count(timbre)) {
        auto file = resource("independent/sounds/" + text(config, "file"));
        auto provenance = readJson(resource("independent/sounds/provenance.json"));
        for (auto &r : provenance.at("banks"))
            if (text(r, "id") == timbre)
                checkFile(file, text(r, "sha256"));
        int bank = library->get<int (*)(void *, const char *, int)>("fluid_synth_sfload")(
            handle, file.getFullPathName().toRawUTF8(), 0);
        require(bank >= 0, "Banque sonore illisible");
        banks[timbre] = bank;
    }
    library->get<int (*)(void *, int)>("fluid_synth_all_sounds_off")(handle, 0);
    require(
        library->get<int (*)(void *, int, int, int, int)>("fluid_synth_program_select")(
            handle, 0, banks[timbre], config.at("preset").at("bank"), config.at("preset").at("program")) >= 0,
        "Preset sonore absent");
    notes.erase(std::remove_if(notes.begin(), notes.end(), [](int n) { return n < 0 || n > 127; }),
                notes.end());
    if (config.value("octaveAdaptation", false) && !notes.empty()) {
        int low = config.at("range")[0], high = config.at("range")[1], shift = 0;
        bool found = false;
        for (int magnitude = 0; magnitude <= 10 && !found; ++magnitude)
            for (int sign : {-1, 1}) {
                int s = magnitude * sign;
                if (std::all_of(notes.begin(), notes.end(),
                                [&](int n) { return n + s * 12 >= low && n + s * 12 <= high; })) {
                    shift = s;
                    found = true;
                    break;
                }
            }
        for (auto &n : notes)
            n = found ? n + shift * 12 : low + (n - low) % 12;
    }
    auto on = library->get<int (*)(void *, int, int, int)>("fluid_synth_noteon");
    for (int n : notes)
        on(handle, 0, n, 90);
    int count = (int)std::max(1., duration * rate);
    std::vector<float> left(count), right(count);
    require(library->get<int (*)(void *, int, void *, int, int, void *, int, int)>("fluid_synth_write_float")(
                handle, count, left.data(), 0, 1, right.data(), 0, 1) >= 0,
            juce::String::fromUTF8("Synthèse impossible"));
    auto off = library->get<int (*)(void *, int, int)>("fluid_synth_noteoff");
    for (int n : notes)
        off(handle, 0, n);
    for (int i = 0; i < count; ++i)
        left[i] = (left[i] + right[i]) * .5f;
    return left;
}
AudioEngine::AudioEngine() { writerThread.startThread(); }
AudioEngine::~AudioEngine() {
    devices.removeAudioCallback(this);
    devices.closeAudioDevice();
    writer.reset();
    writerThread.stopThread(3000);
}
String AudioEngine::initialise(const String &state) {
    auto xml = juce::parseXML(state);
    auto error = devices.initialise(1, 2, xml.get(), true);
    devices.addAudioCallback(this);
    return error;
}
void AudioEngine::setSource(std::shared_ptr<AudioData> data) {
    juce::ScopedLock guard(lock);
    source = std::move(data);
    position = 0;
    timeFactor = 1;
    playing = false;
    loop.reset();
    previewData.clear();
    previewPosition = 0;
}
void AudioEngine::seek(double seconds) {
    juce::ScopedLock guard(lock);
    position = std::clamp(seconds * 16000. / timeFactor, 0., source ? (double)source->mono.size() : 0.);
}
double AudioEngine::seconds() {
    juce::ScopedLock guard(lock);
    return position / 16000. * timeFactor;
}
void AudioEngine::play() {
    juce::ScopedLock guard(lock);
    if (source && !source->mono.empty()) {
        if (position >= source->mono.size())
            position = 0;
        playing = true;
    }
}
void AudioEngine::pause() {
    juce::ScopedLock guard(lock);
    playing = false;
}
void AudioEngine::stop() {
    juce::ScopedLock guard(lock);
    playing = false;
    position = 0;
    previewData.clear();
}
void AudioEngine::preview(std::vector<float> data, double rate) {
    juce::ScopedLock guard(lock);
    previewData = std::move(data);
    previewRate = rate;
    previewPosition = 0;
}
void AudioEngine::record(const File &file) {
    auto *device = devices.getCurrentAudioDevice();
    require(device && device->getActiveInputChannels().countNumberOfSetBits() > 0,
            juce::String::fromUTF8("Choisir une entrée audio active dans le menu."));
    beginRecording(file, device->getActiveInputChannels().countNumberOfSetBits(),
                   device->getCurrentSampleRate());
}
void AudioEngine::beginRecording(const File &file, int inputChannels, double sampleRate) {
    require(inputChannels > 0 && sampleRate > 0, "Entrée audio indisponible");
    auto stream = std::make_unique<juce::FileOutputStream>(file);
    stream->setPosition(0);
    stream->truncate();
    require(stream->openedOk(), juce::String::fromUTF8("Fichier d’enregistrement inaccessible"));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> output = std::move(stream);
    auto raw = format.createWriterFor(
        output,
        juce::AudioFormatWriterOptions().withSampleRate(sampleRate).withNumChannels(1).withBitsPerSample(24));
    require(raw != nullptr, "Enregistrement indisponible");
    juce::ScopedLock guard(lock);
    recordingFile = file;
    outputRate = sampleRate;
    recordingSeconds = 0;
    inputPeak = recordedPeak = 0;
    recordingClipped = false;
    recordingWriteFailed = false;
    recordingPeaks.fill(0);
    recordingPeakIndex = 0;
    writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(raw.release(), writerThread, 32768);
}
File AudioEngine::endRecording() {
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        juce::ScopedLock guard(lock);
        old = std::move(writer);
    }
    old.reset();
    return recordingFile;
}
bool AudioEngine::isRecording() {
    juce::ScopedLock guard(lock);
    return writer != nullptr;
}
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice *device) {
    outputRate = device->getCurrentSampleRate();
}
void AudioEngine::audioDeviceStopped() {
    juce::ScopedLock guard(lock);
    if (writer)
        recordingWriteFailed = true;
}
void AudioEngine::audioDeviceIOCallbackWithContext(const float *const *input, int inputs,
                                                   float *const *output, int outputs, int count,
                                                   const juce::AudioIODeviceCallbackContext &) {
    juce::ScopedLock guard(lock);
    // Preserve incoming samples before writing any output buffers. Backends may alias them.
    inputPeak = 0;
    const auto recordingMultiplier = (float)std::pow(10., recordingGainDB / 20.);
    for (int start = 0; start < count; start += 4096) {
        const auto n = std::min(4096, count - start);
        float buffer[4096]{};
        int activeInputs = 0;
        for (int ch = 0; ch < inputs; ++ch) {
            if (!input || !input[ch])
                continue;
            ++activeInputs;
            for (int i = 0; i < n; ++i) {
                const auto value = std::isfinite(input[ch][start + i]) ? input[ch][start + i] : 0.f;
                inputPeak = std::max(inputPeak, std::abs(value));
                buffer[i] += value;
            }
        }
        if (writer) {
            float peak = 0;
            for (int i = 0; i < n; ++i) {
                const auto amplified = buffer[i] / std::max(1, activeInputs) * recordingMultiplier;
                recordingClipped |= std::abs(amplified) >= 1.f;
                buffer[i] = std::clamp(amplified, -1.f, 1.f);
                peak = std::max(peak, std::abs(buffer[i]));
            }
            const float *channel = buffer;
            if (!writer->write(&channel, n))
                recordingWriteFailed = true;
            recordingSeconds += n / outputRate;
            recordedPeak = std::max(recordedPeak, peak);
            recordingPeaks[recordingPeakIndex++ % recordingPeaks.size()] = peak;
        }
    }
    auto sample = [](const std::vector<float> &data, double at) {
        if (at < 0 || at >= data.size())
            return 0.f;
        auto n = (size_t)at;
        auto fraction = (float)(at - n);
        return data[n] * (1 - fraction) + data[std::min(n + 1, data.size() - 1)] * fraction;
    };
    for (int i = 0; i < count; ++i) {
        float value = 0;
        if (playing && source) {
            value = sample(source->mono, position);
            position += 16000. / outputRate * speed;
            if (loop && position >= loop->second * 16000 / timeFactor)
                position = loop->first * 16000 / timeFactor +
                           std::fmod(position - loop->first * 16000 / timeFactor,
                                     (loop->second - loop->first) * 16000 / timeFactor);
            if (position >= source->mono.size()) {
                playing = false;
                position = (double)source->mono.size();
            }
        }
        if (previewPosition < previewData.size()) {
            value += sample(previewData, previewPosition);
            previewPosition += previewRate / outputRate;
        }
        value = std::clamp(value * (float)gain, -1.f, 1.f);
        for (int ch = 0; ch < outputs; ++ch)
            if (output[ch])
                output[ch][i] = value;
    }
}
void exportWave(const Document &d, Synth &synth, const File &path, Cancel cancel) {
    auto size = (size_t)(d.duration() * synth.rate) + 1;
    require(size <= 238140001, juce::String::fromUTF8("Durée WAV trop longue"));
    std::vector<float> output(size);
    for (auto &c : d.chords()) {
        checkCancel(cancel);
        if (text(c, "name") == "N")
            continue;
        auto notes = c.value("notes", std::vector<int>());
        for (auto &n : notes)
            n += d.j.value("transpose", 0);
        auto wave = synth.chord(notes, c.at("end").get<double>() - c.at("start").get<double>(),
                                text(d.j, "audition_timbre", "upright"));
        auto at = (size_t)(c.at("start").get<double>() * synth.rate);
        for (size_t k = 0; k < wave.size() && at + k < output.size(); ++k)
            output[at + k] += wave[k];
    }
    float peak = 1;
    for (float n : output)
        peak = std::max(peak, std::abs(n));
    for (auto &n : output)
        n /= peak;
    saveWave(path, output, synth.rate);
}
} // namespace kn
