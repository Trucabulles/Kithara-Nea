#include "UI.h"
#include "../common/CpuThreads.h"
#include <psapi.h>
namespace kn {
J verifyDisplay(App &app, const File &output) {
    auto data = std::make_shared<AudioData>();
    data->mono.resize(300 * 16000);
    for (size_t n = 0; n < data->mono.size(); ++n)
        data->mono[n] =
            (float)(.35 * std::sin(n * .084) * (.15 + .85 * std::pow(std::sin(n / 16000. * 2.1), 2)));
    Document doc;
    doc.j["duration"] = 300.;
    for (int n = 0; n < 75; ++n) {
        String name = juce::StringArray{"C", "G", "Am", "F"}[n % 4];
        doc.chords().push_back(
            {{"start", n * 4.}, {"end", n * 4. + 4}, {"name", utf(name)}, {"notes", app.theory.notes(name)}});
    }
    app.setLoaded(doc, data);
    app.timeline.span = 8;
    app.seek(24);
    app.preferredInstrumentHeight = -1;
    app.timelinePixels = 440;
    app.resized();
    {
        juce::ScopedLock lock(app.audio.lock);
        app.audio.playing = true;
    }
    auto start = juce::Time::getMillisecondCounterHiRes();
    const uint64_t peakBuilds = app.timeline.wavePeakBuilds;
    J samples = J::array();
    size_t maxRaster = 0;
    int previous = -1;
    while (true) {
        double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.;
        if (elapsed >= 12)
            break;
        {
            juce::ScopedLock lock(app.audio.lock);
            app.audio.position = (24. + elapsed) * 16000.;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        maxRaster = std::max(maxRaster, app.timeline.waveformBytes());
        if ((int)elapsed != previous) {
            previous = (int)elapsed;
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&memory, sizeof(memory));
            samples.push_back({{"seconds", elapsed},
                               {"privateBytes", memory.PrivateUsage},
                               {"workingSetBytes", memory.WorkingSetSize},
                               {"rasterBytes", app.timeline.waveformBytes()},
                               {"tilesBuilt", app.timeline.waveTileBuilds}});
        }
    }
    {
        juce::ScopedLock lock(app.audio.lock);
        app.audio.playing = false;
    }
    auto capture = app.createComponentSnapshot(app.getLocalBounds());
    juce::PNGImageFormat png;
    auto stream = output.getChildFile("display.png").createOutputStream();
    png.writeImageToStream(capture, *stream);
    J report = {{"samples", samples},
                {"rasterLimitBytes", Timeline::waveCacheLimit},
                {"maximumRasterBytes", maxRaster},
                {"peakBuildsDuringPlayback", app.timeline.wavePeakBuilds - peakBuilds},
                {"tilesBuilt", app.timeline.waveTileBuilds}};
    if (auto *peer = app.getPeer())
        report["renderer"] = utf(peer->getAvailableRenderingEngines()[peer->getCurrentRenderingEngine()]);
    writeJson(output.getChildFile("display-metrics.json"), report);
    require(maxRaster <= Timeline::waveCacheLimit && app.timeline.wavePeakBuilds == peakBuilds,
            "Cache onde non borne ou recalcul pendant lecture");
    app.dirty = false;
    return report;
}
J verifyNative(const File &output) {
    output.createDirectory();
    SetEnvironmentVariableW(L"KITHARA_PROFILE",
                            output.getChildFile("profile").getFullPathName().toWideCharPointer());
    J checks = J::array();
    auto check = [&](String name, bool ok) {
        if (juce::SystemStats::getEnvironmentVariable("KITHARA_TEST_TRACE", {}) == "1")
            output.getChildFile("last-check.txt").replaceWithText(name);
        require(ok, juce::String::fromUTF8("Échec : ") + name);
        checks.push_back(utf(name));
    };
    {
        const auto application = output.getChildFile("portable-fixture");
        application.createDirectory();
        const auto marker = application.getChildFile("portable.flag");
        marker.replaceWithText("");
        check("Portable profile stays beside application",
              profileLocation(application) == application.getChildFile("data"));
        check("Explicit test profile overrides portable mode",
              profileLocation(application, output.getFullPathName()) == output);
        marker.deleteFile();
        check("Installed profile uses user data",
              profileLocation(application) == File::getSpecialLocation(File::userApplicationDataDirectory)
                                                   .getChildFile("kithara-nea"));
        check("Portable first-run language preserves default", defaultUiLanguage(application) == "fr");
        const auto language = application.getChildFile("installation-language.json");
        writeJson(language, {{"uiLanguage", "en"}});
        check("English installer language hint", defaultUiLanguage(application) == "en");
        writeJson(language, {{"uiLanguage", "fr"}});
        check("French installer language hint", defaultUiLanguage(application) == "fr");
        language.replaceWithText("invalid");
        check("Damaged installer hint preserves default", defaultUiLanguage(application) == "fr");
        language.deleteFile();
    }
    {
        for (auto item : {std::pair<unsigned, int>{0, 1}, {1, 1}, {4, 2}, {8, 4}, {12, 6},
                          {16, 8}, {24, 12}, {32, 12}, {128, 12}})
            check("CPU thread budget " + String(item.first), analysisThreadsForCpus(item.first) == item.second);
        const auto available = availableLogicalCpus();
        check("CPU process availability detected", available > 0 && analysisCpuThreads() == analysisThreadsForCpus(available));
        DWORD_PTR originalMask = 0, systemMask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &originalMask, &systemMask) && originalMask) {
            DWORD_PTR restricted = originalMask & (~originalMask + 1);
            const auto remaining = originalMask & ~restricted;
            if (remaining)
                restricted |= remaining & (~remaining + 1);
            if (SetProcessAffinityMask(GetCurrentProcess(), restricted)) {
                const auto limited = availableLogicalCpus();
                const auto budget = analysisCpuThreads();
                const bool restored = SetProcessAffinityMask(GetCurrentProcess(), originalMask) != 0;
                check("CPU affinity restored after verification", restored);
                check("CPU budget respects restricted process affinity", limited <= 2 && budget == 1);
            }
        }
    }
    {
        const auto decoder = File::getSpecialLocation(File::currentExecutableFile);
        const auto resultFile = output.getChildFile("compute-result.json");
        const juce::StringArray cpu{"cpu-decoder", "-dtw", "small", "-nfa", "-ng", "-t",
                                    String(analysisCpuThreads())};
        const J valid = {{"transcription", J::array()}, {"result", {{"language", "en"}}}};
        int probes = 0, gpuRuns = 0, cpuRuns = 0;
        auto success = [&](const juce::StringArray &args, double) {
            if (args.contains("--kithara-gpu-probe")) {
                ++probes;
                return String("KITHARA_GPU_READY=1\nKITHARA_GPU_NAME=Test GPU\n");
            }
            const bool gpu = args[0] == decoder.getFullPathName();
            gpu ? ++gpuRuns : ++cpuRuns;
            const int threadFlag = args.indexOf("-t");
            check("Whisper conserve le budget CPU au changement de backend",
                  threadFlag >= 0 && threadFlag + 1 < args.size() &&
                      args[threadFlag + 1].getIntValue() == analysisCpuThreads());
            check("Whisper flags CPU/GPU coherents", gpu != args.contains("-ng"));
            check("Whisper DTW conserve", args.contains("-dtw") && args.contains("-nfa"));
            if (gpu)
                check("GPU detecte transmis au decodeur",
                      args.contains("-dev") && args[args.size() - 1] == "1");
            writeJson(resultFile, valid);
            return gpu ? String("using Vulkan1 backend") : String("CPU");
        };
        auto result = runWhisperCompute(cpu, decoder, resultFile, "cpu", {}, success);
        check("CPU force sans aucune sonde Vulkan",
              probes == 0 && gpuRuns == 0 && cpuRuns == 1 && result.at("backend") == "cpu");
        result = runWhisperCompute(cpu, decoder, resultFile, "auto", {}, success);
        check("GPU compatible choisi automatiquement", probes == 1 && gpuRuns == 1 &&
                                                           result.at("backend") == "gpu" &&
                                                           !result.at("fallback").get<bool>());
        int attempts = 0;
        auto failure = [&](const juce::StringArray &args, double timeout) {
            if (!args.contains("--kithara-gpu-probe") && args[0] == decoder.getFullPathName()) {
                ++attempts;
                writeJson(resultFile, {{"partial", true}});
                throw std::runtime_error("Simulated device failure");
            }
            if (args[0] == "cpu-decoder")
                check("Resultat GPU partiel efface avant reprise CPU", !resultFile.existsAsFile());
            return success(args, timeout);
        };
        result = runWhisperCompute(cpu, decoder, resultFile, "gpu", {}, failure);
        check("Echec GPU reprend sur CPU et le signale", attempts == 1 && result.at("backend") == "cpu" &&
                                                             result.at("fallback").get<bool>() &&
                                                             result.at("data") == valid);
        auto unavailable = [&](const juce::StringArray &args, double timeout) {
            if (args.contains("--kithara-gpu-probe"))
                throw std::runtime_error("No Vulkan device");
            return success(args, timeout);
        };
        result = runWhisperCompute(cpu, decoder, resultFile, "auto", {}, unavailable);
        check("Machine sans GPU fonctionne en automatique", result.at("backend") == "cpu");
        result = runWhisperCompute(cpu, decoder, resultFile, "gpu", {}, unavailable);
        check("GPU force indisponible signale le repli", result.at("fallback").get<bool>());
        bool cancelled = false;
        const int before = cpuRuns;
        try {
            runWhisperCompute(cpu, decoder, resultFile, "auto", {},
                              [](const juce::StringArray &, double) -> String { throw Cancelled(); });
        } catch (const Cancelled &) {
            cancelled = true;
        }
        check("Annulation GPU ne relance jamais le CPU", cancelled && before == cpuRuns);
    }
    {
        auto token = [](const char *value, int from, int to, int pathFrom, int pathTo) {
            return J{{"id", 42},
                     {"text", value},
                     {"offsets", {{"from", from}, {"to", to}}},
                     {"t_dtw_start", pathFrom},
                     {"t_dtw_end", pathTo}};
        };
        J data = {
            {"result", {{"language", "en"}}},
            {"transcription",
             J::array({{{"text", " Alas, poor leaf"},
                        {"offsets", {{"from", 200}, {"to", 2000}}},
                        {"tokens", J::array({token(" Al", 200, 400, 30, 50), token("as", 400, 600, 50, 70),
                                             token(",", 600, 800, 70, 75), token(" poor", 800, 1200, 90, 110),
                                             token(" leaf", 1200, 2000, 130, 160)})}}})}};
        const auto original = data;
        auto old = parseWhisperLyrics(data, 3, false);
        auto path = parseWhisperLyrics(data, 3, true);
        const auto &cue = path["lyrics"][0];
        check("DTW regroupe sous tokens et ponctuation en mots", cue["words"].size() == 3 &&
                                                                     cue["words"][0]["text"] == "Alas," &&
                                                                     cue["words"][0]["offset"] == 0);
        check("DTW debut et fin tires du chemin", cue["words"][0]["start"] == .3 &&
                                                      cue["words"][0]["end"] == .75 &&
                                                      cue["words"][1]["start"] == .9);
        check("DTW conserve les pauses entre mots",
              cue["words"][1]["end"] == 1.1 && cue["words"][2]["start"] == 1.3);
        check("DTW actualise les bornes de phrase", cue["start"] == .3 && cue["end"] == 1.6);
        check("DTW ne modifie pas le texte ni les sorties brutes",
              data == original && cue["text"] == old["lyrics"][0]["text"]);
        Document document;
        document.j["duration"] = 3.;
        document.lyrics() = path["lyrics"];
        document.validate();
        check("DTW intervalles valides dans le projet", true);
        auto pause = data;
        pause["transcription"].push_back({{"text", " Again here"},
                                          {"offsets", {{"from", 2000}, {"to", 11000}}},
                                          {"tokens", J::array({token(" Again", 2000, 10000, 200, 1000),
                                                               token(" here", 10000, 11000, 1000, 1100)})}});
        auto pauseResult = parseWhisperLyrics(pause, 12., true);
        const auto &leading = pauseResult["lyrics"][1]["words"][0];
        check("DTW attente non attribuee entierement au premier mot",
              leading["dtw_raw_start"] == 2. && std::abs(leading["start"].get<double>() - 9.1) < 1e-8 &&
                  leading["end"] == 10.);
        for (int invalid : {-1, 200}) {
            auto bad = data;
            bad["transcription"][0]["tokens"][2]["t_dtw_start"] = invalid;
            const auto fallback = parseWhisperLyrics(bad, 3, true);
            check("DTW invalide conserve horodatage classique " + String(invalid),
                  fallback["lyrics"][0]["words"] == old["lyrics"][0]["words"] &&
                      fallback["lyrics"][0]["timing_source"] == "token-fallback");
        }
        check("Anticipation exclusivement affichage",
              data == original && (std::abs(lyricDisplayTime(1.) - 1.) < 1e-8 ||
                                   std::abs(lyricDisplayTime(1.) - 1.03) < 1e-8 ||
                                   std::abs(lyricDisplayTime(1.) - 1.06) < 1e-8));
    }
    for (int beatsPerBar : {3, 4})
        for (double tempo : {65., 93., 135., 170.}) {
            std::vector<float> beat(4000, .015f), down(4000, .01f);
            double frames = 3000 / tempo;
            for (int n = 0; n * frames < beat.size(); ++n) {
                int at = (int)std::round(n * frames);
                if (at >= (int)beat.size())
                    break;
                for (int delta = -2; delta <= 2; ++delta) {
                    int x = at + delta;
                    if (x >= 0 && x < (int)beat.size()) {
                        float value = (float)std::exp(-delta * delta / 2.);
                        if (n % 7 != 5)
                            beat[(size_t)x] += .85f * value;
                        if (n % beatsPerBar == 0)
                            down[(size_t)x] += .9f * value;
                    }
                }
                int subdivision = (int)std::round((n + .5) * frames);
                if (subdivision < (int)beat.size())
                    beat[(size_t)subdivision] += .22f;
            }
            Document generated;
            estimateRhythmActivations(generated, beat, down);
            check("BPM activations " + String(tempo) + " mesure " + String(beatsPerBar),
                  std::abs(generated.j["bpm"].get<double>() / tempo - 1) < .03);
            check("Mesure activations " + String(beatsPerBar), generated.j["meter"] == beatsPerBar);
            auto musical = generated.chords();
            generated.j["bpm"] = 81.;
            generated.j["bpm_source"] = "manual";
            generated.j["meter"] = 7;
            generated.j["meter_denominator"] = 8;
            generated.j["meter_source"] = "manual";
            estimateRhythmActivations(generated, beat, down);
            check("Corrections rythmiques manuelles preservees",
                  generated.j["bpm"] == 81. && generated.j["meter"] == 7 &&
                      generated.j["meter_denominator"] == 8 && generated.chords() == musical);
        }
    {
        Document empty;
        std::vector<float> silence(2000, .01f);
        estimateRhythmActivations(empty, silence, silence);
        check("Silence sans estimation rythmique inventee", empty.j["bpm"] == 0 && empty.j["meter"] == 0);
    }
    Theory theory;
    for (int count : {3, 4}) {
        Document rhythm;
        for (int n = 0; n < count * 7; ++n)
            rhythm.j["beats"].push_back(n * .5);
        for (int n = 0; n < 7; ++n)
            rhythm.j["downbeats"].push_back(n * count * .5);
        estimateMeter(rhythm);
        check("Mesure proposee " + String(count) + "/4",
              rhythm.j.at("meter") == count && rhythm.j.at("meter_source") == "estimated");
    }
    {
        Document rhythm;
        estimateMeter(rhythm);
        check("Mesure inconnue sans reperes", rhythm.j.at("meter") == 0);
        for (int n = 0; n < 40; ++n)
            rhythm.j["beats"].push_back(n * .5);
        for (double t : {0., 1.5, 3.5, 5., 7., 8.5, 10., 12.})
            rhythm.j["downbeats"].push_back(t);
        estimateMeter(rhythm);
        check("Mesure ambigue non forcee", rhythm.j.at("meter") == 0);
    }

    for (auto &pair :
         std::vector<std::pair<String, std::vector<int>>>{{"C", {48, 52, 55}},
                                                          {"Cm", {48, 51, 55}},
                                                          {"C5", {48, 55}},
                                                          {"C69", {48, 50, 52, 55, 57}},
                                                          {"C11", {48, 50, 52, 53, 55, 58}},
                                                          {"C13#9", {48, 51, 52, 53, 55, 57, 58}},
                                                          {"C11b5b13", {48, 50, 52, 53, 54, 56, 58}},
                                                          {"C11#5b13", {48, 50, 52, 53, 56, 58}},
                                                          {"C7sus2", {48, 50, 55, 58}},
                                                          {"Cdim7", {48, 51, 54, 57}}})
        check("Intervalles " + pair.first, theory.notes(pair.first) == pair.second);
    check(juce::String::fromUTF8("44 accordages indépendants"), theory.tunings.size() == 44);
    check("Transposition et basse", transpose("Cm7/Eb", 2) == "Dm7/F");
    {
        Document piano;
        J chord = {{"name", "Cm"}, {"bass", ""}, {"notes", J::array({60, 67, 72})}};
        auto voicing = pianoVoicing(chord, piano, theory);
        check("Piano dispose basse et accord complet independamment des cordes",
              voicing.left == std::vector<int>{36} && voicing.right == std::vector<int>({48, 51, 55}));
        chord["name"] = "Ab";
        chord["bass"] = "C";
        auto inversion = pianoVoicing(chord, piano, theory);
        check("Piano respecte basse renversement",
              inversion.left == std::vector<int>{36} && inversion.right == std::vector<int>({56, 60, 63}));
        auto original = piano.j;
        piano.j["capo"] = 7;
        piano.j["instrument_family"] = "banjo";
        piano.j["guitar_tuning"] = "G4,D3,G3,B3,D4";
        check("Piano independant accordage capo et famille cordes",
              pianoVoicing(chord, piano, theory).notes() == inversion.notes());
        piano.j["transpose"] = 2;
        auto shifted = pianoVoicing(chord, piano, theory);
        check("Piano transpose deux mains",
              shifted.left == std::vector<int>{38} && shifted.right == std::vector<int>({58, 62, 65}));
        chord["name"] = "N";
        check("Piano sans accord sans notes", pianoVoicing(chord, piano, theory).notes().empty());
        piano.j = original;
        for (String name : {"C", "Cm7", "Ab", "B", "F#7", "C13", "G7sus4"}) {
            chord["name"] = utf(name);
            chord["bass"] = "";
            auto hands = pianoVoicing(chord, piano, theory);
            check("Piano sans croisement " + name, hands.left.back() < hands.right.front() &&
                                                       hands.left.front() >= 36 && hands.right.back() <= 84);
        }
        juce::Image image(juce::Image::RGB, 1120, 180, true, juce::SoftwareImageType());
        {
            juce::Graphics g(image);
            drawPiano(g, {0, 0, 1120, 180}, voicing.notes(), voicing.left);
        }
        auto stream = output.getChildFile("piano-two-hands.png").createOutputStream();
        juce::PNGImageFormat png;
        png.writeImageToStream(image, *stream);
        writeJson(output.getChildFile("piano-pixels.json"),
                  {{"left", image.getPixelAt(10, 110).toString().toStdString()},
                   {"right", image.getPixelAt(288, 110).toString().toStdString()},
                   {"notes", voicing.notes()}});
        check("Deux couleurs distinctes mains piano",
              image.getPixelAt(10, 110) == juce::Colour(0xff8fb8c7) &&
                  image.getPixelAt(288, 110) == juce::Colour(0xffd2a35e));
    }
    {
        Document chart;
        chart.audioName = "Timed chart reference.wav";
        chart.j["duration"] = 8.;
        chart.j["meter"] = 4;
        chart.j["bpm"] = 120.;
        for (int n = 0; n <= 16; ++n)
            chart.j["beats"].push_back(n * .5);
        chart.j["downbeats"] = J::array({0., 2., 4., 6., 8.});
        auto add = [&](double a, double b, String name) {
            chart.chords().push_back({{"start", a}, {"end", b}, {"name", utf(name)}});
        };
        add(0, .75, "C");
        add(.75, 2, "G");
        add(2, 6, "Am");
        add(6, 6.25, "N");
        add(6.25, 7.4, "F");
        add(7.4, 8, "G");
        const auto before = chart.j;
        auto model = timedChartLayout(chart, theory);
        check("Chart utilise mesures existantes",
              model.at("mode") == "measures" && model.at("bars").size() == 4);
        check("Changement hors temps non quantifie",
              std::abs(model["bars"][0]["fragments"][1]["x0"].get<double>() - .375) < 1e-9);
        check("Accord tenu traverse mesures sans nouvel evenement",
              model["bars"][2]["fragments"][0]["continued"] == true &&
                  model["bars"][2]["fragments"][0]["source_start"] == 2. && chart.chords().size() == 6);
        check("Dictionnaire deduplique doigtés", model.at("dictionary").size() == 4);
        check("Chart conserve absence accord", model["bars"][3]["fragments"][0]["name"] == "N");
        exportTimedChart(chart, theory, output.getChildFile("chart-measures.pdf"));
        writeJson(output.getChildFile("chart-measures.json"), model);
        check("Export chart sans mutation projet", chart.j == before);
        chart.j["beats"][1] = .4;
        chart.j["beats"][2] = .9;
        chart.j["beats"][3] = 1.5;
        auto variable = timedChartLayout(chart, theory);
        check("Chart suit pulsations irregulieres existantes",
              variable.at("mode") == "measures" &&
                  std::abs(variable["bars"][0]["fragments"][1]["x0"].get<double>() - .425) < 1e-9);
        // A swung offbeat is not the start of a bar. Preserve its absolute time.
        chart.j["beats"][1] = .5;
        chart.j["beats"][2] = 1.;
        chart.j["beats"][3] = 1.5;
        chart.chords()[0]["end"] = 1. / 3.;
        chart.chords()[1]["start"] = 1. / 3.;
        auto swing = timedChartLayout(chart, theory);
        check("Attaque swinguee conserve sa place entre temps",
              swing.at("mode") == "measures" &&
                  std::abs(swing["bars"][0]["fragments"][1]["x0"].get<double>() - 1. / 6.) < 1e-9);
        chart.j["meter"] = 0;
        check("Chart refuse absence signature", timedChartLayout(chart, theory).at("mode") == "unavailable");
        bool refused = false;
        try {
            exportTimedChart(chart, theory, output.getChildFile("chart-unavailable.pdf"));
        } catch (const std::exception &) {
            refused = true;
        }
        check("Pas de liste horaire de substitution",
              refused && !output.getChildFile("chart-unavailable.pdf").existsAsFile());
        chart.j["meter"] = 4;
        chart.j["downbeats"] = J::array({0., .5, 2., 4., 6., 8.});
        auto estimated = timedChartLayout(chart, theory);
        check("Chart estime mesures au lieu liste horaire",
              estimated.at("mode") == "measures" && estimated.at("bars").size() == 4 &&
                  estimated.at("reason") == "estimated-constant-tempo");
        chart.j["duration"] = 5.;
        chart.j["beats"] = J::array();
        for (int n = 0; n < 9; ++n)
            chart.j["beats"].push_back(.5 + n * .5);
        chart.j["downbeats"] = J::array({.5, 2.5, 4.5});
        auto partial = timedChartLayout(chart, theory);
        check("Prelude et mesure finale partielle sans extrapolation",
              partial.at("mode") == "measures" && partial["bars"][0]["kind"] == "prelude" &&
                  partial["bars"].back()["kind"] == "partial");
        chart.j["duration"] = 2.;
        chart.j["beats"] = J::array({0., .5, 1., 1.5, 2.});
        chart.j["downbeats"] = J::array({0., 2.});
        chart.chords().clear();
        for (int n = 0; n < 20; ++n)
            add(n * .1, (n + 1) * .1, n % 2 ? "G7" : "Cm7");
        check("Chart conserve changements rapproches",
              timedChartLayout(chart, theory)["bars"][0]["fragments"].size() == 20);
        bool denseRefused = false;
        try {
            exportTimedChart(chart, theory, output.getChildFile("chart-dense.pdf"));
        } catch (const std::exception &) {
            denseRefused = true;
        }
        check("Pas de police microscopique ni empilement pour vingt accords", denseRefused);
        chart.chords().clear();
        const std::vector<String> fourNames{"Cm/G", "Gm/Bb", "Eb/G", "Fm/Ab"};
        for (int n = 0; n < 4; ++n)
            add(n * .5, (n + 1) * .5, fourNames[(size_t)n]);
        const auto fourSource = chart.j;
        auto fourModel = timedChartLayout(chart, theory);
        const auto &typography = fourModel.at("print");
        check("Trois mesures par ligne et taille uniforme lisible",
              typography.at("columns") == 3 && typography.at("font_size").get<float>() >= 10.5f &&
                  typography.at("font_size").get<float>() <= 12);
        const auto &printed = typography.at("bars")[0];
        bool oneLine = printed.size() == 4;
        float right = 8;
        for (const auto &name : printed) {
            oneLine &= name.at("baseline") == 28 && name.at("font_size") == typography.at("font_size") &&
                       name.at("x").get<float>() >= right - .01f;
            right = name.at("x").get<float>() + name.at("width").get<float>();
        }
        check("Quatre noms avec renversements sans chevauchement ni troncature",
              oneLine && right <= 511.f / 3 - 8 + .01f);
        exportTimedChart(chart, theory, output.getChildFile("chart-four-chords-fr.pdf"));
        const bool previousLanguage = isEnglish();
        languageEnglish(true);
        exportTimedChart(chart, theory, output.getChildFile("chart-four-chords-en.pdf"));
        languageEnglish(previousLanguage);
        check("Nouvelle typographie preserve les donnees", chart.j == fourSource);
        chart.j["duration"] = 200.;
        chart.j["beats"] = J::array();
        chart.j["downbeats"] = J::array();
        chart.chords().clear();
        for (int n = 0; n <= 400; ++n)
            chart.j["beats"].push_back(n * .5);
        for (int n = 0; n <= 100; ++n)
            chart.j["downbeats"].push_back(n * 2.);
        for (int n = 0; n < 100; ++n)
            add(n * 2., (n + 1) * 2., n % 2 ? "G" : "C");
        check("Cent mesures sans duplication dictionnaire",
              timedChartLayout(chart, theory)["bars"].size() == 100 &&
                  timedChartLayout(chart, theory)["dictionary"].size() == 2);
        const bool oldLanguage = isEnglish();
        languageEnglish(true);
        exportTimedChart(chart, theory, output.getChildFile("chart-pagination-en.pdf"));
        languageEnglish(oldLanguage);
        chart.j["transpose"] = 2;
        chart.j["capo"] = 3;
        auto transposed = timedChartLayout(chart, theory);
        check("Chart respecte transposition et capo",
              transposed["events"][0]["name"] == "D" && transposed["dictionary"][0]["capo"] == 3);
        auto selectedShapes = theory.positions("D", text(chart.j, "guitar_tuning"), 3);
        if (selectedShapes.size() > 1) {
            chart.j["guitar_shapes"][utf(shapeKey("D", text(chart.j, "guitar_tuning"), 3))] =
                selectedShapes[1];
            check("Chart reutilise position selectionnee",
                  timedChartLayout(chart, theory)["dictionary"][0]["shape"] == selectedShapes[1]);
        }
        chart.j["beats"] = J::array({0., .5, .5});
        check("Chart ne consomme pas pulsations dupliquees",
              timedChartLayout(chart, theory)["reason"] == "estimated-constant-tempo");
    }
    {
        Document strings;
        check("Doigte guitare exact et cordes muettes exclues",
              stringVoicing(strings, {-1, 3, 2, 0, 1, 0}) == std::vector<int>({48, 52, 55, 60, 64}));
        strings.j["capo"] = 2;
        strings.j["transpose"] = 7; // The displayed fingering already incorporates transposition.
        check("Capo applique une fois sans seconde transposition",
              stringVoicing(strings, {-1, 3, 2, 0, 1, 0}) == std::vector<int>({50, 54, 57, 62, 66}));
        strings.j["guitar_tuning"] = "G4,C4,E4,A4";
        strings.j["capo"] = 0;
        check("Accordage rentrant et doublures preserves",
              stringVoicing(strings, {0, 0, 3, 2}) == std::vector<int>({67, 60, 67, 71}));
        for (String family : {"guitar", "bass", "ukulele", "mandolin", "banjo"}) {
            strings.j["instrument_family"] = utf(family);
            check("Son associe famille " + family,
                  stringTimbre(strings) == (family == "guitar" || family == "bass" ? "nylon" : family));
        }
        check("Accordage incompatible ne joue pas notes inventees", stringVoicing(strings, {0, 0}).empty());
    }
    auto shapes = theory.positions("C", "E2,A2,D3,G3,B3,E4", 0);
    check(juce::String::fromUTF8("Positions calculées"), !shapes.empty());
    Document doc;
    doc.j["duration"] = 2.;
    doc.chords().push_back({{"start", 0.},
                            {"end", 2.},
                            {"name", "C"},
                            {"bass", ""},
                            {"confidence", 0.},
                            {"notes", theory.notes("C")},
                            {"source", "manuel"}});
    doc.lyrics().push_back(
        {{"start", 0.},
         {"end", 2.},
         {"text", juce::String::fromUTF8("Été ici")},
         {"words",
          J::array({{{"start", 0.}, {"end", 1.}, {"text", juce::String::fromUTF8("Été")}, {"offset", 0}},
                    {{"start", 1.}, {"end", 2.}, {"text", "ici"}, {"offset", 4}}})}});
    doc.j["song_sheet"] = juce::String::fromUTF8("[C]Été ici\n\nAutre ligne");
    doc.j["reference_text"] = juce::String::fromUTF8("Été ici\n\nAutre ligne");
    doc.validate();
    {
        Document sheet;
        sheet.j["duration"] = 30.;
        sheet.j["reference_text"] = "First line\n\nSecond line";
        sheet.j["reference_matches"] =
            J::array({{{"start", 5.}, {"end", 6.}, {"offset", 0}, {"text", "First"}},
                      {{"start", 6.}, {"end", 7.}, {"offset", 6}, {"text", "line"}},
                      {{"start", 20.}, {"end", 21.}, {"offset", 12}, {"text", "Second"}},
                      {{"start", 21.}, {"end", 22.}, {"offset", 19}, {"text", "line"}}});
        for (auto event : std::vector<std::pair<double, String>>{
                 {0, "C"}, {2, "G"}, {5, "Am"}, {12, "Dm"}, {15, "F"}, {20, "Em"}, {26, "G7"}})
            sheet.chords().push_back(
                {{"start", event.first}, {"end", event.first + 1}, {"name", utf(event.second)}});
        auto result = initialSheet(sheet);
        check("Introduction dans continuite", result.indexOf("[C]") < result.indexOf("First") &&
                                                  result.indexOf("[G]") < result.indexOf("First"));
        check("Interlude entre paroles", result.indexOf("[Dm]") > result.indexOf("First") &&
                                             result.indexOf("[F]") < result.indexOf("Second"));
        check("Conclusion apres paroles", result.indexOf("[G7]") > result.indexOf("Second"));
        check("Pas annexe ni timecodes instrumentaux",
              !result.contains("passages instrumentaux") && !result.contains("12.00 s"));
        check("Sauts de ligne reference preserves", result.contains("line\n\n"));
        check("Passages instrumentaux sans lignes vides ajoutees",
              !result.contains("\n\n\n") && result.startsWith("[C] [G]\n[Am]"));
        writeJson(output.getChildFile("sheet-order.json"), {{"source", result}});
    }
    {
        // Both generation paths must emit changes once, without line reminders.
        Document sheet;
        sheet.j["duration"] = 9.;
        sheet.j["reference_text"] = "First line\nSecond line";
        sheet.j["song_sheet"] = "[Am]User correction\nSecond line";
        sheet.j["reference_matches"] =
            J::array({{{"start", 1.}, {"end", 2.}, {"offset", 0}, {"text", "First"}},
                      {{"start", 2.}, {"end", 3.}, {"offset", 6}, {"text", "line"}},
                      {{"start", 5.}, {"end", 6.}, {"offset", 11}, {"text", "Second"}},
                      {{"start", 6.}, {"end", 7.}, {"offset", 18}, {"text", "line"}}});
        sheet.lyrics() = J::array(
            {{{"start", 1.},
              {"end", 3.},
              {"text", "First line"},
              {"words", J::array({{{"start", 1.}, {"end", 2.}, {"offset", 0}, {"text", "First"}},
                                  {{"start", 2.}, {"end", 3.}, {"offset", 6}, {"text", "line"}}})}},
             {{"start", 5.},
              {"end", 7.},
              {"text", "Second line"},
              {"words", J::array({{{"start", 5.}, {"end", 6.}, {"offset", 0}, {"text", "Second"}},
                                  {{"start", 6.}, {"end", 7.}, {"offset", 7}, {"text", "line"}}})}}});
        sheet.chords() = J::array({{{"start", 1.}, {"end", 9.}, {"name", "C"}}});
        auto before = sheet.j;
        auto verifyPaths = [&](String title, String expected) {
            sheet.j["reference_text"] = before["reference_text"];
            check(title + " texte saisi", initialSheet(sheet) == expected);
            sheet.j["reference_text"] = "";
            check(title + " transcription", initialSheet(sheet) == expected);
            sheet.j["reference_text"] = before["reference_text"];
        };
        verifyPaths("Accord tenu sans rappel", "[C]First line\nSecond line");
        J diagnostic = {{"held_chord", {{"start", 1.}, {"end", 9.}}},
                        {"source_text_result", utf(initialSheet(sheet))}};
        sheet.j["reference_text"] = "";
        diagnostic["transcription_result"] = utf(initialSheet(sheet));
        sheet.j["reference_text"] = before["reference_text"];
        sheet.chords()[0]["start"] = 2.8;
        verifyPaths("Changement dans mot sans rappel voisin", "First [C]line\nSecond line");
        diagnostic["late_word_change"] = {
            {"chord_start", 2.8}, {"word_start", 2.}, {"word_end", 3.}, {"result", utf(initialSheet(sheet))}};
        check("Diagnostic conserve song sheet retouchee", sheet.j["song_sheet"] == before["song_sheet"]);
        writeJson(output.getChildFile("sheet-placement-diagnostic.json"), diagnostic);
        auto setChanges = [&](std::vector<std::pair<double, String>> changes) {
            sheet.chords().clear();
            for (size_t i = 0; i < changes.size(); ++i)
                sheet.chords().push_back({{"start", changes[i].first},
                                          {"end", i + 1 < changes.size() ? changes[i + 1].first : 9.},
                                          {"name", utf(changes[i].second)}});
        };
        setChanges({{0., "C"}, {1., "G"}, {3., "Am"}, {5., "F"}, {7., "G"}});
        verifyPaths("Introduction interlude conclusion une seule fois",
                    "[C]\n[G]First line\n[Am]\n[F]Second line\n[G]");
        auto automatic = sheet;
        automatic.j["song_sheet"] = "";
        exportChordPro(automatic, output.getChildFile("sheet-changes-only.cho"));
        check("ChordPro automatique sans rappels",
              output.getChildFile("sheet-changes-only.cho")
                  .loadFileAsString()
                  .contains("[C]\n[G]First line\n[Am]\n[F]Second line\n[G]"));
        exportPdf(automatic, theory, output.getChildFile("sheet-changes-only.pdf"), true);
        setChanges({{1., "C"}, {2., "G"}, {3., "Am"}, {5., "F"}});
        verifyPaths("Frontieres mots et lignes sans anticipation", "[C]First [G]line\n[Am]\n[F]Second line");
        setChanges({{1., "C"}, {1.2, "N"}, {1.3, "C"}});
        verifyPaths("Retour meme accord apres silence conserve", "[C][C]First line\nSecond line");
        setChanges({{1., "C"}, {1.5, "G"}, {1.8, "Am"}});
        verifyPaths("Plusieurs changements meme mot ordre conserve", "[C][G][Am]First line\nSecond line");
        for (auto &cue : sheet.lyrics())
            cue["words"] = J::array();
        sheet.j["reference_text"] = "";
        setChanges({{0., "C"}, {1., "G"}, {2., "Am"}, {4., "F"}});
        check("Sans mots horodates pas de rappel de ligne",
              initialSheet(sheet) == "[C]\n[G]First line\n[Am]\n[F]\nSecond line");
        sheet.j["reference_text"] = before["reference_text"];
        setChanges({{1., "C"}});
        sheet.j["reference_matches"] = J::array();
        check("Texte sans reperes conserve sans mot invente",
              initialSheet(sheet) == "First line\nSecond line\n[C]");
        exportChordPro(sheet, output.getChildFile("edited-sheet-preserved.cho"));
        check("Export conserve retouches sans regenerer", output.getChildFile("edited-sheet-preserved.cho")
                                                              .loadFileAsString()
                                                              .contains("[Am]User correction\nSecond line"));
    }
    for (String breaks : {String("\n"), String("\n\n"), String("\n\n\n"), String("\r\n\r\n")}) {
        Document sheet;
        const String source = "\nFirst" + breaks + "Second\n";
        sheet.j["reference_text"] = utf(source);
        sheet.j["reference_matches"] = J::array(
            {{{"start", 0.}, {"end", 1.}, {"offset", source.indexOf("First")}, {"text", "First"}},
             {{"start", 10.}, {"end", 11.}, {"offset", source.indexOf("Second")}, {"text", "Second"}}});
        sheet.chords().push_back({{"start", 0.}, {"end", 20.}, {"name", "C"}});
        check("Retours saisis conserves exactement " + String(breaks.length()) +
                  (breaks.containsChar('\r') ? " CRLF" : " LF"),
              initialSheet(sheet).replace("[C]", "") == source);
    }
    auto original = doc.j;
    auto jsonFile = output.getChildFile("project.json");
    doc.save(jsonFile);
    check("JSON compatible", Document::load(jsonFile).j == doc.j);
    std::vector<float> signal(32000);
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = (float)(.1 * std::sin(i * .1));
    auto wav = output.getChildFile(juce::String::fromUTF8("Source été.wav"));
    saveWave(wav, signal, 16000);
    doc.j["audio"] = utf(wav.getFullPathName());
    doc.audioName = wav.getFileName();
    auto container = output.getChildFile("autonome.knea");
    doc.save(container);
    auto loaded = Document::load(container);
    check(juce::String::fromUTF8("Audio intégré préservé"), sha256(loaded.audio()) == sha256(wav));
    check(juce::String::fromUTF8("Données du projet restaurées"),
          loaded.chords() == doc.chords() && loaded.lyrics() == doc.lyrics() &&
              loaded.j["song_sheet"] == doc.j["song_sheet"]);
    auto light = output.getChildFile("leger.knea");
    doc.save(light, false);
    check(juce::String::fromUTF8("Audio externe restauré"), Document::load(light).audio() == wav);
    {
        auto take = doc;
        take.j["audio_origin"] = "recording";
        const auto path = output.getChildFile("take_linked.knea");
        const auto hash = sha256(wav);
        const auto originalTake = output.getChildFile("original-take.wav");
        require(wav.copyFileTo(originalTake), "Copie prise test impossible");
        take.j["audio"] = utf(originalTake.getFullPathName());
        take.save(path, false);
        require(originalTake.deleteFile(), "Suppression prise test impossible");
        auto reopened = Document::load(path);
        check("Prise integree meme en sauvegarde liee", reopened.mediaMode == "embedded" && reopened.ownsAudio());
        check("Prise liee conserve octets audio", sha256(reopened.audio()) == hash);
        auto relocated = output.getChildFile("relocated");
        relocated.createDirectory();
        const auto moved = relocated.getChildFile("take_linked.knea");
        require(path.copyFileTo(moved), "Test copie projet impossible");
        auto portableTake = Document::load(moved);
        check("Prise projet deplace reste autonome", sha256(portableTake.audio()) == hash);
        portableTake.save(relocated.getChildFile("again_linked.knea"), false);
        auto again = Document::load(relocated.getChildFile("again_linked.knea"));
        check("Prise reouverte puis liee reste integree", again.mediaMode == "embedded" && sha256(again.audio()) == hash);
    }
    History history;
    history.push(doc);
    auto edited = doc;
    edited.j["transpose"] = 2;
    check("Annuler", history.undo(edited).j == doc.j);
    check(juce::String::fromUTF8("Rétablir"), history.redo(doc).j == edited.j);
    auto result = align(juce::String::fromUTF8("Été ici\n\nAutre ligne"), doc.lyrics());
    writeJson(output.getChildFile("alignment-debug.json"),
              J{{"matches", result.first}, {"lyrics", result.second}});
    check("Alignement sur mots existants", result.first.size() == 2 && result.second.size() == 1);
    check(juce::String::fromUTF8("Offsets Unicode conservés"), result.first[1]["offset"] == 4);
    check(juce::String::fromUTF8("Mots manquants sans temps inventé"),
          result.second[0]["text"] == juce::String::fromUTF8("Été ici"));
    exportChordPro(doc, output.getChildFile("song.cho"));
    check("ChordPro et lignes vides", output.getChildFile("song.cho")
                                          .loadFileAsString()
                                          .contains(juce::String::fromUTF8("[C]Été ici\n\nAutre ligne")));
    exportMidi(doc, output.getChildFile("song.mid"));
    check("Export MIDI", output.getChildFile("song.mid").getSize() > 40);
    exportCsv(doc, output.getChildFile("song.csv"));
    exportPdf(doc, theory, output.getChildFile("song.pdf"), true);
    check("Export PDF", output.getChildFile("song.pdf").getSize() > 500);
    auto golden = resource("native-tests/golden.json");
    if (golden.existsAsFile()) {
        auto data = readJson(golden);
        for (auto &c : data.at("notes"))
            check(juce::String::fromUTF8("Référence ") + text(c, "name"),
                  theory.notes(text(c, "name"), text(c, "bass")) == c.at("notes").get<std::vector<int>>());
        for (auto &c : data.at("shapes"))
            check(juce::String::fromUTF8("Doigtés ") + text(c, "name") + " " + text(c, "tuning"),
                  theory.positions(text(c, "name"), text(c, "tuning"), c.at("capo")) ==
                      c.at("shapes").get<std::vector<std::vector<int>>>());
        for (auto &c : data.at("alignment")) {
            auto aligned = align(text(c, "text"), c.at("recognized"));
            check(juce::String::fromUTF8("Alignement de référence"),
                  aligned.first == c.at("matches") && aligned.second == c.at("lyrics"));
        }
    }

    auto floats = [](const File &file) {
        juce::MemoryBlock data;
        require(file.loadFileAsData(data) && data.getSize() % sizeof(float) == 0, "Fixture float invalide");
        std::vector<float> result(data.getSize() / sizeof(float));
        std::memcpy(result.data(), data.getData(), data.getSize());
        return result;
    };
    J differences = J::object();
    auto compare = [&](String name, const std::vector<float> &actual, const std::vector<float> &expected,
                       double tolerance) {
        check(name + " dimensions", actual.size() == expected.size());
        double peak = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            require(std::isfinite(actual[i]), "Float invalide");
            peak = std::max(peak, (double)std::abs(actual[i] - expected[i]));
        }
        differences[utf(name)] = peak;
        check(name, peak < tolerance);
    };
    compare("Reechantillonnage SciPy",
            resample(floats(resource("native-tests/resample-input.f32")), 44100, 16000),
            floats(resource("native-tests/resample-expected.f32")), .000002);
    AudioData rhythm;
    rhythm.mono = floats(resource("native-tests/beat-input.f32"));
    compare("Spectrogramme rythmique", beatFeatures(rhythm, {}),
            floats(resource("native-tests/beat-features.f32")), .0001);
    writeJson(output.getChildFile("numeric-differences.json"), differences);
    {
        AudioEngine engine;
        auto samples = std::make_shared<AudioData>();
        samples->mono.assign(32000, .25f);
        engine.setSource(samples);
        engine.timeFactor = 2;
        engine.seek(1);
        check("Curseur apres vitesse", std::abs(engine.seconds() - 1) < 1e-9);
        engine.play();
        std::array<float, 512> left{}, right{};
        float *outputs[] = {left.data(), right.data()};
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, {});
        check("Lecture native produit du son", *std::max_element(left.begin(), left.end()) > .1f);
        check("Curseur avance avec la lecture", engine.seconds() > 1);
        engine.stop();
        check("Retour debut", engine.seconds() == 0);
    }
    {
        AudioEngine capture;
        auto path = output.getChildFile("recording-alias.wav");
        capture.beginRecording(path, 1, 48000);
        std::array<float, 4096> incoming{};
        for (size_t i = 0; i < incoming.size(); ++i)
            incoming[i] = .3f * std::sin((float)i * .08f);
        const float *input[] = {incoming.data()};
        float *sameOutput[] = {incoming.data()};
        capture.audioDeviceIOCallbackWithContext(input, 1, sameOutput, 1, (int)incoming.size(), {});
        check("Capture mesure duree et niveau reels", capture.recordingSeconds > .08 && capture.inputPeak > .29f);
        check("Capture ecriture acceptee", !capture.recordingWriteFailed);
        capture.endRecording();
        auto saved = loadAudio(path, {}, 48000);
        float peak = 0;
        for (auto value : saved->mono)
            peak = std::max(peak, std::abs(value));
        check("Capture conserve entree avant sorties partagees", peak > .29f);
        check("Capture conserve nombre echantillons", saved->mono.size() == incoming.size());
        path = output.getChildFile("recording-null-channel.wav");
        capture.beginRecording(path, 2, 48000);
        incoming.fill(.25f);
        const float *sparseInputs[] = {nullptr, incoming.data()};
        capture.audioDeviceIOCallbackWithContext(sparseInputs, 2, nullptr, 0, (int)incoming.size(), {});
        capture.endRecording();
        saved = loadAudio(path, {}, 48000);
        check("Capture ne divise pas par canaux inactifs", std::abs(saved->mono[100] - .25f) < 1e-6f);
        capture.recordingGainDB = 6;
        capture.beginRecording(output.getChildFile("recording-gain.wav"), 1, 48000);
        incoming.fill(.25f);
        capture.audioDeviceIOCallbackWithContext(input, 1, nullptr, 0, (int)incoming.size(), {});
        capture.endRecording();
        saved = loadAudio(output.getChildFile("recording-gain.wav"), {}, 48000);
        check("Gain entree applique au WAV", std::abs(saved->mono[100] - .25f * std::pow(10.f, .3f)) < 1e-6f);
        check("Gain ecoute independant capture", capture.gain == .7);
        capture.recordingGainDB = 24;
        capture.beginRecording(output.getChildFile("recording-clipped.wav"), 1, 48000);
        incoming.fill(.5f);
        capture.audioDeviceIOCallbackWithContext(input, 1, nullptr, 0, (int)incoming.size(), {});
        check("Saturation entree detectee", capture.recordingClipped && capture.recordedPeak == 1.f);
        capture.endRecording();
    }
    {
        Synth synth;
        exportWave(doc, synth, output.getChildFile("accompagnement.wav"));
        check("Export WAV audible", loadAudio(output.getChildFile("accompagnement.wav"))->seconds() > 1.9);
        for (auto &sound : synth.catalog.at("timbres")) {
            auto wave = synth.chord({48, 52, 55}, .4, text(sound, "id"));
            float peak = 0;
            for (auto f : wave) {
                checkCancel({});
                require(std::isfinite(f), juce::String::fromUTF8("Échantillon invalide"));
                peak = std::max(peak, std::abs(f));
            }
            check("Son " + text(sound, "id"), peak > .001f);
        }
        for (auto id : {String("mandolin"), String("banjo")}) {
            std::vector<float> demo;
            for (const auto &notes :
                 std::vector<std::vector<int>>{{36, 40, 43}, {48, 51, 55}, {55, 59, 62}, {84, 88, 91}}) {
                auto wave = synth.chord(notes, .7, id);
                double energy = 0;
                bool finite = true;
                for (float value : wave) {
                    finite &= std::isfinite(value);
                    energy += value * value;
                }
                check("Registre sonore " + id + " " + String(notes.front()),
                      finite && std::sqrt(energy / wave.size()) > .0001);
                demo.insert(demo.end(), wave.begin(), wave.end());
                demo.insert(demo.end(), (size_t)(synth.rate * .15), 0.f);
            }
            saveWave(output.getChildFile(id + "-chords.wav"), demo, synth.rate);
        }
    }
    {
        App app(false);
        app.setSize(1500, 1100);
        app.setLoaded(doc, loadAudio(wav));
        {
            auto before = app.doc.j;
            auto count = dataFolder().findChildFiles(File::findFiles, false, "Prise-*.wav").size();
            app.dirty = false;
            app.recordAudio();
            check("Decompte avant toute capture", app.recordingDeadline > 0 && !app.audio.isRecording());
            auto countdownImage = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            auto countdownStream = output.getChildFile("recording-countdown.png").createOutputStream();
            juce::PNGImageFormat().writeImageToStream(countdownImage, *countdownStream);
            app.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
            check("Espace annule decompte sans fichier",
                  app.recordingDeadline == 0 && !app.audio.isRecording() &&
                  dataFolder().findChildFiles(File::findFiles, false, "Prise-*.wav").size() == count);
            app.recordAudio();
            app.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            check("Echap annule decompte sans changement", app.recordingDeadline == 0 && app.doc.j == before);
            app.recordAudio();
            app.recordAudio();
            check("Second clic annule decompte", app.recordingDeadline == 0);
            app.inputGain.setValue(6, juce::sendNotificationSync);
            check("Commande gain entree independante volume", app.audio.recordingGainDB == 6 && app.volume.getValue() == 70);
            app.inputGain.setValue(0, juce::sendNotificationSync);
        }
        {
            const auto before = app.doc.j;
            app.audio.beginRecording(output.getChildFile("recording-ui.wav"), 1, 48000);
            std::array<float, 512> incoming{};
            for (int block = 0; block < 40; ++block) {
                for (size_t i = 0; i < incoming.size(); ++i)
                    incoming[i] = .25f * std::sin((float)(i + block * incoming.size()) * .09f);
                const float *input[] = {incoming.data()};
                app.audio.audioDeviceIOCallbackWithContext(input, 1, nullptr, 0, (int)incoming.size(), {});
            }
            app.timerCallback();
            check("Compteur prise affiche duree capturee", !app.clock.getText().contains("/") && app.clock.getText().contains("0:00.4"));
            auto image = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            auto stream = output.getChildFile("recording-live.png").createOutputStream();
            juce::PNGImageFormat().writeImageToStream(image, *stream);
            app.audio.endRecording();
            check("Retour visuel prise ne modifie pas le projet", app.doc.j == before);
            app.timerCallback();
        }
        {
            const auto take = output.getChildFile("space-recording.wav");
            app.audio.beginRecording(take, 1, 48000);
            std::array<float, 4096> incoming{};
            incoming.fill(.2f);
            const float *input[] = {incoming.data()};
            app.audio.audioDeviceIOCallbackWithContext(input, 1, nullptr, 0, (int)incoming.size(), {});
            check("Espace arrete prise", app.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)) && !app.audio.isRecording());
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000;
            while (app.jobActive && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            check("Espace ouvre prise comme audio interne", !app.jobActive && app.doc.ownsAudio() && app.doc.audio() == take && app.dirty);
            app.setLoaded(doc, loadAudio(wav));
        }
        {
            const auto loaded = app.doc;
            const auto audioBefore = app.source;
            app.jobActive = true;
            app.newProject();
            check("Nouveau projet bloque pendant traitement",
                  app.doc.j == loaded.j && app.source == audioBefore);
            app.jobActive = false;
            app.dirty = true;
            juce::Timer::callAfterDelay(80, [] {
                if (auto *modal = juce::Component::getCurrentlyModalComponent())
                    modal->exitModalState(0);
            });
            app.newProject();
            check("Annuler nouveau projet conserve travail",
                  app.doc.j == loaded.j && app.source == audioBefore && app.dirty);
            app.dirty = false;
            app.loop.setToggleState(true, juce::dontSendNotification);
            app.newProject();
            check("Nouveau projet vide sans analyse",
                  !app.source && app.doc.chords().empty() && app.doc.lyrics().empty() &&
                      app.project == File() && !app.dirty && !app.jobActive && !app.loop.getToggleState() &&
                      app.history.back.empty());
            check("Nouvelle transcription reste raccourci audio",
                  app.newButton.getButtonText() == tr("Nouvelle transcription"));
            app.setLoaded(loaded, audioBefore);
        }
        verifySheetEditor(app, output);
        {
            const auto savedDoc = app.doc;
            const auto savedAudio = app.source;
            Document aligned = savedDoc;
            aligned.chords().clear();
            aligned.j["song_sheet"] = "";
            aligned.j.erase("song_sheet_generated");
            app.setLoaded(aligned, savedAudio);
            auto matched = align(juce::String::fromUTF8("Été ici"), savedDoc.lyrics());
            app.doc.j["reference_text"] = utf(juce::String::fromUTF8("Été ici"));
            JobResult alignment;
            alignment.payload = {{"matches", matched.first},
                                 {"lyrics", matched.second},
                                 {"language", "fr"},
                                 {"recognized_segments", 1}};
            app.finishAlignment(alignment);
            const auto lyricData = app.doc.lyrics(), references = app.doc.j["reference_matches"];
            JobResult harmony;
            harmony.document = app.doc;
            harmony.document->chords() = savedDoc.chords();
            app.finishChordAnalysis(harmony);
            const auto alignmentFirst = text(app.doc.j, "song_sheet");
            check("Aligner avant analyser conserve tous les temps",
                  app.doc.lyrics() == lyricData && app.doc.j["reference_matches"] == references);
            check("Aligner avant analyser ajoute accords sans Whisper",
                  alignmentFirst.contains("[C]") && alignmentFirst == initialSheet(app.doc));
            auto chordFirst = savedDoc;
            chordFirst.j["song_sheet"] = "";
            chordFirst.j.erase("song_sheet_generated");
            chordFirst.j["reference_text"] = utf(juce::String::fromUTF8("Été ici"));
            app.setLoaded(chordFirst, savedAudio);
            app.finishAlignment(alignment);
            check("Ordre des analyses meme song sheet", text(app.doc.j, "song_sheet") == alignmentFirst);
            app.doc.j["song_sheet"] = "[Am]My manual sheet";
            const auto edited = app.doc.j;
            harmony.document = app.doc;
            harmony.document->chords()[0]["name"] = "G";
            app.finishChordAnalysis(harmony);
            check("Analyse preserve sheet manuelle", text(app.doc.j, "song_sheet") == "[Am]My manual sheet" &&
                                                         app.doc.lyrics() == edited["lyrics"]);
            app.undo();
            check("Undo analyse restaure accords et sheet", app.doc.j == edited);
            app.redo();
            app.finishAlignment(alignment);
            check("Realignement preserve retouches song sheet",
                  text(app.doc.j, "song_sheet") == "[Am]My manual sheet");

            app.setLoaded(savedDoc, savedAudio);
            app.doc.j["song_sheet"] = "";
            app.doc.j["reference_text"] = utf(juce::String::fromUTF8("Été ici"));
            app.doc.j["reference_matches"] = matched.first;
            app.doc.lyrics() = matched.second;
            auto autoDoc = app.doc;
            updateAutomaticSheet(app.doc, autoDoc);
            String error;
            {
                const auto beforeCell = app.doc.j;
                std::unique_ptr<juce::Component> cell(
                    app.chordTable.refreshComponentForCell(0, 3, false, nullptr));
                auto *label = dynamic_cast<juce::Label *>(cell.get());
                check("Cellule accord editable double clic", label && label->isEditableOnDoubleClick());
                label->setText("Dm7", juce::sendNotificationSync);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                check("Edition cellule applique callback", text(app.doc.chords()[0], "name") == "Dm7");
                app.undo();
                check("Undo cellule restaure projet entier", app.doc.j == beforeCell);
            }
            const auto beforeWord = app.doc;
            check("Mot edite sans perdre ses temps", app.applyWordCell(0, 0, 3, "Summer", error) &&
                                                         text(app.doc.lyrics()[0], "text") == "Summer ici" &&
                                                         app.doc.lyrics()[0]["words"][0]["start"] ==
                                                             beforeWord.lyrics()[0]["words"][0]["start"]);
            check("Reference et offsets suivent mot corrige",
                  text(app.doc.j, "reference_text") == "Summer ici" &&
                      app.doc.j["reference_matches"][1]["offset"] == 7);
            check("Song sheet auto suit mot corrige", text(app.doc.j, "song_sheet").contains("Summer ici"));
            app.undo();
            check("Undo mot restaure source temps et sheet", app.doc.j == beforeWord.j);
            app.redo();
            check("Temps mot modifiable",
                  app.applyWordCell(0, 0, 2, "0.8", error) && app.doc.j["reference_matches"][0]["end"] == .8);
            const auto beforeBad = app.doc.j;
            check("Chevauchement mot refuse sans mutation",
                  !app.applyWordCell(0, 0, 2, "1.5", error) && app.doc.j == beforeBad);
            check("Nombre invalide refuse sans mutation",
                  !app.applyWordCell(0, 0, 1, "NaN", error) && app.doc.j == beforeBad);
            check("Accord cellule editable", app.applyDataCell(0, false, 3, "Gm7/Bb", error) &&
                                                 text(app.doc.chords()[0], "name") == "Gm7" &&
                                                 text(app.doc.chords()[0], "bass") == "Bb");
            check("Basse cellule editable",
                  app.applyDataCell(0, false, 4, "D", error) && text(app.doc.chords()[0], "bass") == "D");
            check("Basse mal formee refusee", !app.applyDataCell(0, false, 4, "Da", error));
            check("Mauvaise fondamentale refusee", !app.applyDataCell(0, false, 3, "XYZ", error));
            check("Temps cellule editable",
                  app.applyDataCell(0, false, 2, "1,9", error) && app.doc.chords()[0]["end"] == 1.9);
            check("Ligne editable source coherente",
                  app.applyDataCell(0, true, 3, "New full line", error) &&
                      text(app.doc.j, "reference_text") == "New full line" &&
                      app.doc.lyrics()[0]["words"].empty() && app.doc.j["reference_matches"].empty());
            app.undo();
            app.editWords(0);
            auto *dialog = app.activeDialog();
            check("Editeur mots ouvert", dialog && dialog->getName() == tr("Modifier les mots horodatés"));
            auto image = dialog->createComponentSnapshot(dialog->getLocalBounds());
            auto stream = output.getChildFile("word-editor-fr.png").createOutputStream();
            juce::PNGImageFormat().writeImageToStream(image, *stream);
            dialog->closeButtonPressed();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            app.setLanguage(true);
            app.toggleDetails();
            app.tabs.setCurrentTabIndex(1);
            auto tableImage =
                app.editingWindow()->createComponentSnapshot(app.editingWindow()->getLocalBounds());
            auto tableStream = output.getChildFile("edit-data-en.png").createOutputStream();
            juce::PNGImageFormat().writeImageToStream(tableImage, *tableStream);
            app.editWords(0);
            dialog = app.activeDialog();
            image = dialog->createComponentSnapshot(dialog->getLocalBounds());
            auto englishStream = output.getChildFile("word-editor-en.png").createOutputStream();
            juce::PNGImageFormat().writeImageToStream(image, *englishStream);
            dialog->closeButtonPressed();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            app.editingWindow()->closeButtonPressed();
            app.setLanguage(false);
            auto file = output.getChildFile("manual-data-corrections.knea");
            app.doc.save(file, false);
            check("Corrections et statut auto sauves knea", Document::load(file).j == app.doc.j);
            app.setLoaded(savedDoc, savedAudio);
        }
        app.timeline.audioVisible = false;
        app.timeline.lyricsVisible = false;
        app.timeline.span = 2;
        auto image = app.createComponentSnapshot(app.getLocalBounds());
        juce::PNGImageFormat png;
        auto stream = output.getChildFile("application.png").createOutputStream();
        stream->setPosition(0);
        stream->truncate();
        png.writeImageToStream(image, *stream);
        int before = app.timeline.getHeight(), height = app.instruments.getHeight();
        auto saved = app.doc.j;
        app.toggleDetails();
        check("Fenetre de donnees non modale", app.editingWindow() && app.editingWindow()->isVisible() &&
                                                   !app.editingWindow()->isCurrentlyModal());
        app.tabs.setCurrentTabIndex(1);
        app.editingWindow()->closeButtonPressed();
        check("Fermeture donnees", !app.editingWindow()->isVisible());
        app.toggleDetails();
        check("Onglet conserve", app.tabs.getCurrentTabIndex() == 1);
        check("Timeline stable avec editeur", before == app.timeline.getHeight());
        check("Instruments stables avec editeur", height == app.instruments.getHeight());
        app.editingWindow()->closeButtonPressed();
        check("Piano a gauche du diagramme",
              app.instruments.pianoRect.getRight() < app.instruments.diagramRect.getX());
        check("Representations cote a cote",
              app.instruments.pianoRect.getY() < app.instruments.diagramRect.getBottom());
        check("Boutons 5 secondes absents", !app.back.isVisible() && !app.forward.isVisible());
        bool settingsVisible = app.settingsView.isVisible();
        app.options.onClick();
        check("Options replie le panneau", app.settingsView.isVisible() != settingsVisible);
        app.options.onClick();
        check("Options restaure le panneau", app.settingsView.isVisible() == settingsVisible);
        check("Langues et vitesse masquees", !app.settings.language.isVisible() &&
                                                 !app.settings.speed.isVisible() &&
                                                 !app.settings.adapt.isVisible());
        app.instruments.createComponentSnapshot(app.instruments.getLocalBounds());
        auto key = app.instruments.pianoKeys.front();
        auto point = juce::Point<float>(key.second.getCentreX(), key.second.getBottom() - 10);
        app.instruments.pressKeyAt(point);
        check("Touche cliquee illuminee", app.instruments.highlightedKey() == 36);
        auto pressed = app.instruments.createComponentSnapshot(app.instruments.getLocalBounds());
        {
            auto file = output.getChildFile("piano-pressed-diagnostic.png").createOutputStream();
            png.writeImageToStream(pressed, *file);
            writeJson(output.getChildFile("piano-pressed-diagnostic.json"),
                      {{"x", point.x},
                       {"y", point.y},
                       {"key", app.instruments.pressedKey},
                       {"colour", pressed.getPixelAt((int)point.x, (int)point.y).toString().toStdString()}});
        }
        check("Couleur touche blanche Phthongos",
              pressed.getPixelAt((int)point.x, (int)point.y - 20) == juce::Colour(0xffd2a35e));
        app.instruments.pressedKey = -1;
        app.instruments.repaint();

        auto black = std::find_if(app.instruments.pianoKeys.begin(), app.instruments.pianoKeys.end(),
                                  [](auto &key) { return key.first == 37; });
        auto blackPoint = black->second.getCentre();
        app.instruments.pressKeyAt(blackPoint);
        auto blackImage = app.instruments.createComponentSnapshot(app.instruments.getLocalBounds());
        check("Couleur touche noire Phthongos",
              blackImage.getPixelAt((int)blackPoint.x, (int)blackPoint.y) == juce::Colour(0xffb8813c));
        app.instruments.pressedKey = -1;
        app.doc.j["key"] = "C majeur";
        app.doc.j["transpose"] = 1;
        app.refresh();
        check("Tonalite avant et apres",
              app.settings.keyFrom.getText().contains("Do majeur") &&
                  app.settings.keyTo.getText().contains(juce::String::fromUTF8("Ré♭ majeur")));
        {
            const auto previous = app.doc.j;
            for (bool english : {false, true}) {
                app.setLanguage(english);
                app.doc.j["key"] = "Eb mineur";
                app.doc.j["transpose"] = 0;
                app.refresh();
                check("Tonalite transposee masquee a zero " + String((int)english),
                      !app.settings.keyTo.isVisible());
                auto captureKey = [&](int semitones) {
                    auto image = app.settings.createComponentSnapshot({0, 108, app.settings.getWidth(), 198});
                    auto file = output
                                    .getChildFile(String("key-") + (english ? "en-" : "fr-") +
                                                  String(semitones) + ".png")
                                    .createOutputStream();
                    png.writeImageToStream(image, *file);
                };
                captureKey(0);
                const auto before = app.doc.j;
                app.settings.transpose.setValue(1, juce::sendNotificationSync);
                check("Tonalite transposee affichee apres plus " + String((int)english),
                      app.settings.keyTo.isVisible() && app.doc.j.at("transpose") == 1 &&
                          app.doc.j.at("key") == before.at("key"));
                captureKey(1);
                app.undo();
                check("Annulation restaure tonalite sans ligne double " + String((int)english),
                      !app.settings.keyTo.isVisible() && app.doc.j == before);
            }
            app.doc.j = previous;
            app.setLanguage(false);
        }
        app.doc.j["key"] = "";
        app.doc.j["transpose"] = 0;
        app.refresh();
        {
            bool oldPatternRejected = false;
            try {
                std::regex oldPattern("\\[([A-G](?:#|b)?[^]\\n]*)\\]");
            } catch (const std::regex_error &) {
                oldPatternRejected = true;
            }
            writeJson(output.getChildFile("transpose-diagnostic.json"),
                      {{"previous_pattern_rejected", oldPatternRejected}});
            auto original = app.doc.j;
            app.doc.j["song_sheet"] = utf(juce::String::fromUTF8("[C]Été [G/B]ici\n\n[Am7]Encore."));
            auto beforeTranspose = app.doc.j;
            app.settings.transpose.setValue(1, juce::sendNotificationSync);
            check("Bouton transposition plus avec song sheet",
                  app.doc.j.at("transpose") == 1 &&
                      text(app.doc.j, "song_sheet") ==
                          juce::String::fromUTF8("[C#]Été [Ab/C]ici\n\n[Bbm7]Encore."));
            check("Transposition preserve paroles et horodatages",
                  app.doc.j.at("lyrics") == beforeTranspose.at("lyrics") &&
                      app.doc.chords() == beforeTranspose.at("chords"));
            app.settings.transpose.setValue(0, juce::sendNotificationSync);
            check("Bouton transposition moins avec song sheet", app.doc.j == beforeTranspose);
            app.undo();
            check("Annulation transposition moins", app.doc.j.at("transpose") == 1);
            app.undo();
            check("Annulation transposition plus", app.doc.j == beforeTranspose);
            app.doc.j = original;
            app.refresh();
        }
        auto notesBefore = app.doc.chords();
        app.settings.signature.setSelectedId(8, juce::sendNotificationSync);
        check("Mesure corrigeable en 6/8", app.doc.j.at("meter") == 6 &&
                                               app.doc.j.at("meter_denominator") == 8 &&
                                               app.doc.j.at("meter_source") == "manual");
        check("Mesure ne deplace pas les accords", app.doc.chords() == notesBefore);
        app.undo();
        auto beforeTime = app.doc.j;
        String error;
        check("Horodatage invalide refuse",
              !app.applyTimes(0, false, -1, 2, error) && app.doc.j == beforeTime);
        check("Correction temporelle appliquee",
              app.applyTimes(0, false, .1, 1.8, error) && app.doc.chords()[0]["start"] == .1);
        app.undo();

        auto eventFor = [](juce::Component &component, juce::Point<float> position, int clicks) {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                                    juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, &component,
                                    &component, juce::Time::getCurrentTime(), position,
                                    juce::Time::getCurrentTime(), clicks, false);
        };
        auto initial = app.doc;
        auto originalAudio = app.source;
        check("Menu de grilles sans piano",
              app.instruments.family.getNumItems() == 5 && app.instruments.family.indexOfItemId(2) < 0);
        check("Libelle du choix instrumental",
              app.instruments.familyCaption.getText().contains("Visualiser"));
        check("Nombre de positions explicite",
              app.instruments.positionCountText().contains(String((int)app.shapes.size())) &&
                  app.instruments.positionCountText().contains("disponibles") &&
                  app.instruments.positionCountText().contains("cliquer pour changer"));
        auto captions = app.instruments.positionCaptionBounds();
        check("Legendes du diagramme dans leur panneau", app.instruments.diagramRect.contains(captions));
        check("Commandes ecoute rapprochees du piano",
              app.instruments.audition.getY() >= app.instruments.pianoRect.getBottom() &&
                  app.instruments.audition.getY() - app.instruments.pianoRect.getBottom() <= 16);
        check("Ecoute cordes dans coin inferieur gauche du panneau",
              app.instruments.diagramRect.contains(app.instruments.stringsAudition.getBounds().toFloat()) &&
                  app.instruments.stringsAudition.getX() == (int)app.instruments.diagramRect.getX() + 8 &&
                  app.instruments.stringsAudition.getBottom() ==
                      (int)app.instruments.diagramRect.getBottom() - 8);
        {
            auto beforeListen = app.doc.j;
            auto shapeBefore = app.shapeIndex;
            app.instruments.audition.onClick();
            check("Bouton piano charge piano et produit du son",
                  app.synth && app.synth->banks.count("upright") && !app.audio.previewData.empty());
            app.instruments.stringsAudition.onClick();
            check("Bouton cordes charge guitare et produit du son",
                  app.synth && app.synth->banks.count("nylon") && !app.audio.previewData.empty());
            check("Ecoute ne change ni projet ni doigté",
                  app.doc.j == beforeListen && app.shapeIndex == shapeBefore);
            auto point = app.instruments.stringsAudition.getBounds().getCentre();
            check("Haut parleur intercepte son clic sans changer position",
                  app.instruments.getComponentAt(point) == &app.instruments.stringsAudition);
        }
        check("Nom accord a mi hauteur entre choix et piano",
              std::abs(app.instruments.chordNameBounds().getCentreY() -
                       (app.instruments.family.getBottom() + app.instruments.pianoRect.getY()) * .5f) < .01f);
        check("Nom accord ne touche ni menu ni piano",
              app.instruments.chordNameBounds().getY() > app.instruments.family.getBottom() &&
                  app.instruments.chordNameBounds().getBottom() < app.instruments.pianoRect.getY());
        app.doc.j["reference_text"] = "Texte saisi conserve";
        auto beforeFailure = app.doc.j;
        JobResult emptyAlignment;
        emptyAlignment.payload = {{"matches", J::array()},
                                  {"lyrics", J::array()},
                                  {"language", "auto"},
                                  {"recognized_segments", 0}};
        app.finishAlignment(emptyAlignment);
        check("Sans paroles alignement explicitement impossible",
              app.status.contains("alignement impossible"));
        check("Echec sans perte des paroles et texte", app.doc.j == beforeFailure);
        emptyAlignment.payload["recognized_segments"] = 1;
        app.finishAlignment(emptyAlignment);
        check("Aucun rapprochement distingue du silence", app.status.contains("Aucun repère alignable"));
        check("Rapprochement vide ne modifie pas le document", app.doc.j == beforeFailure);
        auto alignedResult = align(juce::String::fromUTF8("Été ici"), initial.lyrics());
        JobResult goodAlignment;
        goodAlignment.payload = {{"matches", alignedResult.first},
                                 {"lyrics", alignedResult.second},
                                 {"language", "fr"},
                                 {"recognized_segments", 1}};
        app.timeline.lyricsVisible = false;
        app.finishAlignment(goodAlignment);
        app.createComponentSnapshot(app.getLocalBounds());
        check("Alignement reussit et revele la piste",
              app.timeline.lyricsVisible && !app.timeline.lyricHits.empty());
        check("Alignement produit les horodatages attendus", app.doc.lyrics() == alignedResult.second);
        app.setLoaded(initial, originalAudio);
        app.timelineManual = false;
        app.preferredInstrumentHeight = -1;
        app.timelinePixels = 280;
        app.resized();
        int defaultHeight = app.timeline.getHeight();
        auto splitterDown = eventFor(app.split, {4, 2}, 1);
        app.split.mouseDown(splitterDown);
        app.split.mouseDrag(splitterDown.withNewPosition(juce::Point<float>(4, 402)));
        check("Poignee permet un agrandissement manuel superieur",
              app.timelineManual && app.timeline.getHeight() > defaultHeight + 20);
        check("Poignee garde les commandes inferieures accessibles", app.instruments.getHeight() >= 240);
        check("Redimensionnement ne change pas les evenements", app.doc.j == initial.j);
        app.timelineManual = false;
        app.preferredInstrumentHeight = -1;
        app.timelinePixels = 280;
        app.resized();
        check("Options aligne avec titre analyse", app.options.getY() == app.settingsView.getY() + 3);
        check("Edition donnees apres paroles dans panneau",
              app.details.getParentComponent() == &app.settings &&
                  app.details.getY() > app.settings.reportButton.getBottom());
        app.timeline.lyricsVisible = true;
        app.timeline.position = .5;
        app.timeline.offset = 0;
        app.timeline.span = 2;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Phrase complete unique et mots cliquables",
              app.timeline.lyricHits.size() == 3 &&
                  std::get<2>(app.timeline.lyricHits.front()) == juce::String::fromUTF8("Été ici"));
        auto highlightPixels = [&](const juce::Image &image, juce::Rectangle<float> rect) {
            auto bounds = rect.toNearestInt().getIntersection(image.getBounds());
            int count = 0;
            for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
                for (int x = bounds.getX(); x < bounds.getRight(); ++x)
                    if (image.getPixelAt(x, y) == app.theme.selected)
                        ++count;
            return count;
        };
        auto firstActive = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        check("Premier mot surligne sans case de duree",
              highlightPixels(firstActive, std::get<0>(app.timeline.lyricHits[1])) > 10 &&
                  highlightPixels(firstActive, std::get<0>(app.timeline.lyricHits[2])) == 0);
        auto stableFirstWord = std::get<0>(app.timeline.lyricHits[1]);
        auto phraseBox = std::get<0>(app.timeline.lyricHits.front());
        auto phraseLeft = std::get<0>(app.timeline.lyricHits[1]).getX();
        auto phraseRight = std::get<0>(app.timeline.lyricHits[2]).getRight();
        check("Phrase centree dans sa bande",
              std::abs((phraseLeft + phraseRight) * .5f - phraseBox.getCentreX()) < 2.f);
        app.timeline.position = 1.5;
        auto secondActive = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        check("Surbrillance suit le second mot horodate",
              highlightPixels(secondActive, std::get<0>(app.timeline.lyricHits[1])) == 0 &&
                  highlightPixels(secondActive, std::get<0>(app.timeline.lyricHits[2])) > 10);
        check("Phrase stable quand le mot courant change",
              stableFirstWord == std::get<0>(app.timeline.lyricHits[1]));
        const auto beforeAnticipation = app.doc.j;
        app.timeline.position = .98;
        auto anticipation = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        const bool advanced = lyricDisplayTime(.98) >= 1.;
        check("Anticipation surligne au seuil visuel exact",
              (highlightPixels(anticipation, std::get<0>(app.timeline.lyricHits[2])) > 10) == advanced);
        const double beforeSwitch = 1. - (lyricDisplayTime(1.) - 1.) - .01;
        app.timeline.refreshPlayback(beforeSwitch);
        app.timeline.refreshPlayback(beforeSwitch + .02);
        check("Anticipation invalide la bande paroles au changement de mot",
              !advanced ||
                  std::any_of(app.timeline.playbackRegions.begin(), app.timeline.playbackRegions.end(),
                              [&](const auto &area) {
                                  return area.toFloat().intersects(std::get<0>(app.timeline.lyricHits[2]));
                              }));
        check("Anticipation conserve donnees et horodatages", app.doc.j == beforeAnticipation);
        auto lyricsBeforeGap = app.doc.j;
        app.doc.lyrics()[0]["end"] = 4.; // A coarse phrase end must not keep completed words alive.
        app.timeline.position = 1.99;
        auto finalWordImage = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        check("Dernier mot reste affiche jusqu a sa fin reelle",
              app.timeline.lyricHits.size() == 3 &&
                  highlightPixels(finalWordImage, std::get<0>(app.timeline.lyricHits[2])) > 10);
        app.timeline.refreshPlayback(2.);
        check("Fin du mot invalide la bande meme sans prochain texte",
              std::any_of(app.timeline.playbackRegions.begin(), app.timeline.playbackRegions.end(),
                          [&](const auto &area) {
                              return area.toFloat().intersects(std::get<0>(app.timeline.lyricHits[2]));
                          }));
        app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        check("Phrase disparait a la fin du dernier mot pas au debut", app.timeline.lyricHits.empty());
        app.doc.j = lyricsBeforeGap;
        app.timeline.position = 2.5;
        auto gapImage = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        check("Zone vide apres dernier mot sans message", app.timeline.lyricHits.empty());
        check("Masquage phrase ne change pas horodatages", app.doc.j == lyricsBeforeGap);
        {
            auto stream = output.getChildFile("lyrics-ended.png").createOutputStream();
            png.writeImageToStream(gapImage, *stream);
        }
        app.timeline.position = 1.5;
        app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        auto lyricPreview = output.getChildFile("phrase-continue.png").createOutputStream();
        png.writeImageToStream(secondActive, *lyricPreview);
        app.timeline.position = .5;
        float firstWordWidth = std::get<0>(app.timeline.lyricHits[1]).getWidth();
        auto lyricDocument = app.doc.j;
        app.timeline.span = 1;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Espacement naturel independant du zoom temporel",
              std::abs(std::get<0>(app.timeline.lyricHits[1]).getWidth() - firstWordWidth) < .01f);
        check("Phrase continue preserve les horodatages", app.doc.j == lyricDocument);
        auto secondWord = std::get<0>(app.timeline.lyricHits[2]).getCentre();
        app.timeline.mouseDown(eventFor(app.timeline, secondWord, 1));
        check("Clic sur mot continu cherche son temps", std::abs(app.audio.seconds() - 1.) < 1e-8);
        {
            auto gapDoc = app.doc;
            auto following = gapDoc.lyrics()[0];
            following["start"] = 4.;
            following["end"] = 6.;
            following["text"] = "Following phrase";
            following["words"] = J::array();
            gapDoc.lyrics().push_back(following);
            app.setLoaded(gapDoc, originalAudio);
            app.timeline.position = 3.;
            app.timeline.audioVisible = false;
            app.timeline.chordsVisible = false;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            auto active = std::find_if(app.timeline.lyricRows.begin(), app.timeline.lyricRows.end(),
                                       [](auto row) { return row.current; });
            check("Paroles seules retirent phrase pendant blanc",
                  active == app.timeline.lyricRows.end() && app.timeline.lyricRows.empty());
            app.timeline.position = 4.5;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            active = std::find_if(app.timeline.lyricRows.begin(), app.timeline.lyricRows.end(),
                                  [](auto row) { return row.current; });
            check("Phrase suivante remplace ancienne a son debut",
                  active != app.timeline.lyricRows.end() && active->index == 1);
            app.timeline.position = 5.99;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            check("Phrase sans temps de mots reste jusqu a sa fin",
                  std::any_of(app.timeline.lyricRows.begin(), app.timeline.lyricRows.end(),
                              [](auto row) { return row.current; }));
            app.timeline.position = 6.;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            check("Paroles seules vides apres dernier chant", app.timeline.lyricRows.empty());
            app.timeline.position = -.1;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            check("Avant premier chant aucun faux texte",
                  std::none_of(app.timeline.lyricRows.begin(), app.timeline.lyricRows.end(),
                               [](auto row) { return row.current; }));
            app.timeline.audioVisible = true;
            app.timeline.chordsVisible = true;
        }
        app.setLoaded(initial, originalAudio);
        {
            App probe(false);
            probe.setLoaded(initial, originalAudio);
            probe.doc.chords().clear();
            probe.dirty = true;
            probe.jobActive = true; // Inspect the queue without executing a neural model.
            probe.analyseButton.onClick();
            check("Premiere analyse sans demande de sauvegarde",
                  probe.queue.size() == 1 && !probe.activeDialog());
            probe.queue.clear();
            probe.doc = initial;
            probe.dirty = true;
            auto beforeQuestion = probe.doc.j;
            auto answer = [&](String button) {
                auto *dialog = probe.activeDialog();
                require(dialog != nullptr, "Question analyse absente");
                auto *content = dialog->getContentComponent();
                for (int n = 0; n < content->getNumChildComponents(); ++n)
                    if (auto *b = dynamic_cast<juce::TextButton *>(content->getChildComponent(n)))
                        if (b->getButtonText() == button) {
                            b->onClick();
                            return;
                        }
                require(false, "Bouton de reponse absent");
            };
            probe.analyseButton.onClick();
            check("Analyse existante question specifique",
                  probe.activeDialog() && probe.activeDialog()->getName() == "Analyse existante");
            answer("Non");
            check("Non conserve tout sans lancer analyse",
                  probe.queue.empty() && probe.doc.j == beforeQuestion);
            probe.analyseButton.onClick();
            answer("Oui");
            check("Oui lance analyse sans demande enregistrement",
                  probe.queue.size() == 1 && !probe.activeDialog());
            JobResult replacement;
            replacement.document = initial;
            replacement.document->chords()[0]["name"] = "G";
            replacement.document->chords()[0]["notes"] = theory.notes("G");
            auto completion = probe.queue.front().complete;
            probe.queue.clear();
            probe.jobActive = false;
            completion(replacement);
            check("Analyse applique zoom de lisibilite",
                  std::abs(probe.timeline.span - probe.timeline.readableSpan()) < 1e-8);
            check("Nouvelle analyse conserve paroles", probe.doc.lyrics() == initial.lyrics());
            probe.undo();
            check("Remplacement des accords annulable", probe.doc.j == beforeQuestion);
        }
        auto transport = std::make_shared<AudioData>();
        transport->mono.assign(20 * 16000, .1f);
        auto transportDoc = app.doc;
        transportDoc.j["duration"] = 20.;
        app.setLoaded(transportDoc, transport);
        app.seek(12.5);
        app.play.onClick();
        app.play.onClick();
        check("Pause garde le curseur", !app.audio.playing && std::abs(app.audio.seconds() - 12.5) < 1e-9 &&
                                            std::abs(app.timeline.position - 12.5) < 1e-9);
        app.play.onClick();
        check("Reprise meme position", app.audio.playing && std::abs(app.audio.seconds() - 12.5) < 1e-9);
        app.audio.pause();
        app.begin.onClick();
        check("Arriere moins cinq secondes", std::abs(app.audio.seconds() - 7.5) < 1e-9);
        app.end.onClick();
        check("Avant plus cinq secondes", std::abs(app.audio.seconds() - 12.5) < 1e-9);
        auto backEvent = eventFor(app.begin, {10, 10}, 2);
        const auto beforeBack = app.audio.seconds();
        app.begin.mouseDown(backEvent);
        const auto afterDown = app.audio.seconds();
        app.begin.mouseDoubleClick(backEvent);
        app.begin.mouseUp(backEvent);
        writeJson(output.getChildFile("transport-diagnostic.json"),
                  {{"before", beforeBack},
                   {"after_down", afterDown},
                   {"after_up", app.audio.seconds()},
                   {"clicks", backEvent.getNumberOfClicks()},
                   {"callback", (bool)app.begin.onDoubleClick},
                   {"duration", app.doc.duration()},
                   {"playing", app.audio.playing}});
        check("Double clic arriere debut", app.audio.seconds() == 0);
        auto nextEvent = eventFor(app.end, {10, 10}, 2);
        app.end.mouseDown(nextEvent);
        app.end.mouseDoubleClick(nextEvent);
        app.end.mouseUp(nextEvent);
        check("Double clic avant fin", app.audio.seconds() == 20);
        app.timeline.offset = 10;
        auto marginEvent = eventFor(app.timeline, {15, 90}, 2);
        app.timeline.mouseDoubleClick(marginEvent);
        check("Double clic marge retour debut", app.audio.seconds() == 0 && app.timeline.offset == 0);
        app.setLoaded(initial, originalAudio);
        app.setSize(1200, 900);
        app.createComponentSnapshot(app.getLocalBounds());
        float smallerDetail = app.instruments.detailBounds().getHeight();
        float smallerMini =
            app.timeline.miniBoxes.empty() ? 0 : app.timeline.miniBoxes.front().second.getHeight();
        app.setSize(1800, 1300);
        app.createComponentSnapshot(app.getLocalBounds());
        check("Grandir agrandit le diagramme", app.instruments.detailBounds().getHeight() > smallerDetail);
        check("Grandir ne reduit pas les mini diagrammes",
              app.timeline.miniBoxes.empty() ||
                  app.timeline.miniBoxes.front().second.getHeight() >= smallerMini);
        // Regression: wide/short screens and an oversized restored splitter
        // preference.
        app.timeline.audioVisible = true;
        app.timeline.lyricsVisible = true;
        app.preferredInstrumentHeight = -1;
        app.timelinePixels = 1400;
        app.timeline.span = 2;
        app.timeline.offset = 0;
        float previousMini = 0;
        for (int width : {1200, 1600, 2000, 2560}) {
            app.setSize(width, 940);
            auto capture = app.createComponentSnapshot(app.getLocalBounds());
            check("Detail lisible ecran large " + String(width),
                  app.instruments.detailBounds().getHeight() >= 60.f);
            check("Police accord lisible " + String(width), app.timeline.chordFontHeight() >= 20.f);
            check("Police paroles lisible " + String(width), app.timeline.lyricFontHeight() >= 18.f);
            check("Diagramme visible ecran large " + String(width), !app.timeline.miniBoxes.empty());
            float currentMini = app.timeline.miniBoxes.front().second.getHeight();
            check("Largeur croissante diagramme monotone " + String(width), currentMini >= previousMini);
            previousMini = currentMini;
            auto file = output.getChildFile("play-along-" + String(width) + ".png").createOutputStream();
            png.writeImageToStream(capture, *file);
        }
        auto playAlongDoc = initial;
        playAlongDoc.j["duration"] = 24.;
        playAlongDoc.chords().clear();
        playAlongDoc.lyrics().clear();
        int index = 0;
        for (const String name : {"C", "G", "Am", "F", "Dm7", "G7"}) {
            double start = index++ * 4.;
            playAlongDoc.chords().push_back(
                {{"start", start}, {"end", start + 4}, {"name", utf(name)}, {"notes", theory.notes(name)}});
            playAlongDoc.lyrics().push_back(
                {{"start", start}, {"end", start + 4}, {"text", "Suivre les paroles en jouant"}});
        }
        auto playAlongAudio = std::make_shared<AudioData>();
        playAlongAudio->mono.resize(24 * 16000);
        app.setLoaded(playAlongDoc, playAlongAudio);
        app.timeline.span = 12;
        for (int height : {940, 1280}) {
            app.setSize(2000, height);
            auto capture = app.createComponentSnapshot(app.getLocalBounds());
            check("Plusieurs diagrammes visibles " + String(height), app.timeline.miniBoxes.size() >= 3);
            auto file =
                output.getChildFile("play-along-context-" + String(height) + ".png").createOutputStream();
            png.writeImageToStream(capture, *file);
        }
        app.setSize(1800, 1200);
        for (int width : {1000, 1500, 2000}) {
            app.setSize(width, 1200);
            check("Volume et gain alignes sans collision " + String(width),
                  app.volume.getBounds().getCentreY() == app.inputGain.getBounds().getCentreY() &&
                      app.volume.getRight() + 16 <= app.inputGainLabel.getX() &&
                      app.inputGain.getRight() <= app.options.getRight() &&
                      app.volume.getWidth() >= 40 && app.inputGain.getWidth() >= 100);
        }
        app.setSize(1800, 1200);
        app.timeline.position = 1;
        app.timeline.span = 12;
        const auto waveDoc = app.doc.j;
        // A non-silent waveform makes overlay readability visible in the comparison captures.
        for (size_t n = 0; n < playAlongAudio->mono.size(); ++n) {
            const double t = n / 16000.;
            const double envelope = .06 + .40 * std::pow(.5 + .5 * std::sin(t * 2.3), 3);
            playAlongAudio->mono[n] = (float)(envelope * (std::sin(t * 1382.) + .3 * std::sin(t * 2191.)));
        }
        app.timeline.prepareWaveform();
        app.timeline.setSize(app.timeline.getWidth() - 1, app.timeline.getHeight());
        auto captureView = [&](String name) {
            auto capture = app.createComponentSnapshot(app.getLocalBounds());
            auto file = output.getChildFile(name + ".png").createOutputStream();
            png.writeImageToStream(capture, *file);
        };
        captureView("wave-chords-visible");
        {
            const double originalOffset = app.timeline.offset;
            const double originalPosition = app.timeline.position;
            const double density = (app.timeline.getWidth() - 70.) / app.timeline.span;
            auto changes = app.timeline.chordChanges();
            const double at = app.doc.chords()[(size_t)changes[1]].at("start");
            app.timeline.offset = at - app.timeline.span + 20. / density;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            bool entersPartially = false;
            for (auto &entry : app.timeline.waveChordLabels)
                entersPartially |= entry.first == at && entry.second.getX() < app.timeline.getWidth() - 15 &&
                                   entry.second.getRight() > app.timeline.getWidth() - 15;
            check("Nom accord entre progressivement par le bord droit", entersPartially);
            app.timeline.offset = at + 10. / density;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            bool pinnedAtLeft = false;
            for (auto &entry : app.timeline.waveChordLabels)
                pinnedAtLeft |= entry.first == at && entry.second.getX() == 59;
            check("Accord tenu conserve son nom au bord gauche", pinnedAtLeft);
            app.timeline.position = 0;
            app.timeline.offset = 2. + 8. / density;
            auto ticksBefore = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            app.timeline.offset += 1. / density;
            auto ticksAfter = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            int tickDifferences = 0;
            for (int y = 3; y < 22; ++y)
                for (int x = 55; x < ticksBefore.getWidth() - 17; ++x)
                    tickDifferences += ticksBefore.getPixelAt(x + 1, y) != ticksAfter.getPixelAt(x, y);
            check("Timecodes glissent sans apparition brusque aux bords", tickDifferences == 0);
            app.timeline.offset = originalOffset;
            app.timeline.position = originalPosition;
            auto cursorImage = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            const auto band = app.timeline.bands();
            const float centre = 35.f + std::max(1.f, std::round(band[0] - 42.f)) * .5f;
            const int cursorX = (int)std::round(app.timeline.x(originalPosition));
            bool redBelow = false;
            for (int y = (int)std::ceil(centre) + 2; y < (int)band[0]; ++y)
                redBelow |= cursorImage.getPixelAt(cursorX, y) == juce::Colour(0xffa94d3f);
            check("Tete de lecture arretee au centre onde",
                  !redBelow && cursorImage.getPixelAt(cursorX, 28) == juce::Colour(0xffa94d3f));
            auto file = output.getChildFile("player-volume-short-cursor.png").createOutputStream();
            png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()), *file);
        }
        auto waveLabels = app.timeline.waveChordLabels;
        check("Accords sur onde aux temps exacts",
              waveLabels.size() >= 3 &&
                  std::abs(waveLabels[1].second.getX() - app.timeline.x(waveLabels[1].first) - 4) < .01f);
        app.timeline.chordsVisible = false;
        captureView("wave-chords-hidden");
        check("Noms sur onde independants de piste accords",
              app.timeline.waveChordLabels == waveLabels && app.timeline.miniBoxes.empty());
        app.timeline.audioVisible = false;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Masquer onde masque ses noms et reperes",
              app.timeline.waveChordLabels.empty() && app.timeline.waveChordMarkers.empty());
        app.timeline.audioVisible = true;
        app.timeline.chordsVisible = true;
        {
            auto held = playAlongDoc;
            held.chords().clear();
            held.j["duration"] = 36.;
            auto addChord = [&](Document &d, double start, double end, String name) {
                d.chords().push_back({{"start", start},
                                      {"end", end},
                                      {"name", utf(name)},
                                      {"notes", name == "N" ? std::vector<int>() : theory.notes(name)}});
            };
            addChord(held, 0, 20, "C");
            addChord(held, 20, 24, "G");
            addChord(held, 24, 26, "N");
            addChord(held, 26, 28, "C");
            addChord(held, 32, 36, "Am");
            app.setLoaded(held, playAlongAudio);
            const auto heldData = app.doc.j;
            app.timeline.span = 8;
            auto hasPinned = [&](double offset, double time) {
                app.timeline.offset = offset;
                app.timeline.position = offset + 1;
                app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
                return std::any_of(
                    app.timeline.waveChordLabels.begin(), app.timeline.waveChordLabels.end(),
                    [&](const auto &entry) { return entry.first == time && entry.second.getX() == 59; });
            };
            check("Nom maintenu pendant accord long sans marqueur fictif",
                  hasPinned(8, 0) && app.timeline.waveChordMarkers.empty());
            captureView("wave-held-chord-left");
            check("Nom maintenu stable au defilement", hasPinned(10, 0));
            check("Rappel remplace au changement reel",
                  hasPinned(20.5, 20) && app.timeline.waveChordLabels.front().first == 20);
            app.timeline.offset = 25;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            check("Aucun accord ancien maintenu pendant N",
                  std::none_of(app.timeline.waveChordLabels.begin(), app.timeline.waveChordLabels.end(),
                               [](const auto &entry) { return entry.first < 25; }));
            app.timeline.offset = 30;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            check("Aucun accord ancien maintenu pendant intervalle vide",
                  std::none_of(app.timeline.waveChordLabels.begin(), app.timeline.waveChordLabels.end(),
                               [](const auto &entry) { return entry.first < 30; }));
            check("Rappel ne change pas donnees musicales", app.doc.j == heldData);
            app.timeline.audioVisible = false;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            check("Rappel absent sans waveform", app.timeline.waveChordLabels.empty());
            app.timeline.audioVisible = true;
            app.setLoaded(playAlongDoc, playAlongAudio);
        }
        auto dense = playAlongDoc;
        dense.chords().clear();
        for (int n = 0; n < 160; ++n)
            dense.chords().push_back({{"start", n * .15},
                                      {"end", (n + 1) * .15},
                                      {"name", n % 2 ? "G7" : "Cm7"},
                                      {"notes", theory.notes(n % 2 ? "G7" : "Cm7")}});
        app.setLoaded(dense, playAlongAudio);
        app.timeline.span = 24;
        captureView("wave-changes-dense");
        check("Changements denses gardent tous les marqueurs", app.timeline.waveChordMarkers.size() == 160);
        bool noOverlap = true;
        for (size_t n = 1; n < app.timeline.waveChordLabels.size(); ++n)
            noOverlap &= app.timeline.waveChordLabels[n].second.getX() >=
                         app.timeline.waveChordLabels[n - 1].second.getRight() + 8;
        check("Noms denses sans chevauchement", noOverlap && app.timeline.waveChordLabels.size() < 160);
        app.setLoaded(playAlongDoc, playAlongAudio);
        app.timeline.span = 8;
        app.timeline.offset = 2;
        app.timeline.position = 3;
        auto one = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        uint64_t peakBuilds = app.timeline.wavePeakBuilds, tileBuilds = app.timeline.waveTileBuilds;
        app.timeline.offset += app.timeline.span / (app.timeline.getWidth() - 70);
        auto two = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
        int differencesPixels = 0;
        int y0 = 83, y1 = std::max(y0, (int)app.timeline.bands()[0] - 10);
        // Exclude marker/text rows and the red cursor; compare the same source pixels after a one-pixel
        // scroll.
        for (int y = y0; y < y1; ++y)
            for (int x = 100; x < app.timeline.getWidth() - 100; ++x)
                if (std::abs(x - app.timeline.x(3)) > 8 && std::abs(x - 1 - app.timeline.x(3)) > 8)
                    differencesPixels += one.getPixelAt(x, y) != two.getPixelAt(x - 1, y);
        check("Onde stable au deplacement entier", differencesPixels == 0);
        check("Defilement reutilise tuiles sans recalcul audio",
              app.timeline.wavePeakBuilds == peakBuilds && app.timeline.waveTileBuilds <= tileBuilds + 1);
        for (int n = 0; n < 100; ++n) {
            app.timeline.offset = n * .17;
            app.timeline.createComponentSnapshot(app.timeline.getLocalBounds());
            check("Cache raster borne", app.timeline.waveformBytes() <= Timeline::waveCacheLimit);
        }
        app.timeline.refreshPlayback(18.1);
        app.timeline.refreshPlayback(18.15);
        bool outsideCards = true;
        auto band = app.timeline.bands();
        for (auto area : app.timeline.playbackRegions)
            outsideCards &= area.getBottom() <= (int)std::ceil(band[0]);
        check("Lecture ne repeint ni grilles ni paroles stables", outsideCards);
        auto highDpi = app.timeline.createComponentSnapshot(app.timeline.getLocalBounds(), true, 2.f);
        check("Raster DPI natif reste borne", app.timeline.waveformBytes() <= Timeline::waveCacheLimit);
        auto dpiStream = output.getChildFile("waveform-dpi2.png").createOutputStream();
        png.writeImageToStream(highDpi, *dpiStream);
        app.setLoaded(playAlongDoc, playAlongAudio);
        app.setLoaded(playAlongDoc, playAlongAudio);
        app.timelineManual = true;
        app.preferredInstrumentHeight = -1;
        app.timelinePixels = 10000;
        app.resized();
        int bottomLimit = app.timeline.getHeight();
        app.preferredInstrumentHeight = -1;
        app.timelinePixels = 20000;
        app.resized();
        check("Poignee bornee au maximum bas", app.timeline.getHeight() == bottomLimit);
        check("Zone instrumentale preservee au maximum",
              app.instruments.getHeight() >= 240 &&
                  app.instruments.audition.getBottom() <= app.instruments.getHeight() &&
                  app.instruments.diagramRect.contains(app.instruments.positionCaptionBounds()));
        {
            auto originalSize = juce::Point<int>(app.getWidth(), app.getHeight());
            auto originalDocument = app.doc.j;
            auto originalShape = app.shapeIndex;
            auto originalSelection = app.selected;
            app.preferredInstrumentHeight = 300;
            app.timelineManual = true;
            app.setSize(1400, 1000);
            app.resized();
            const int wished = app.instruments.getHeight();
            auto ratio = app.instruments.pianoRect.getWidth() / app.instruments.pianoRect.getHeight();
            check("Piano proportionnel deux axes", std::abs(ratio - 1015.f / 162) < .01f);
            app.setSize(1100, 780);
            auto smallTimeline = app.timeline.getHeight();
            check("Panneau compact en petite fenetre",
                  app.instruments.getHeight() < wished &&
                      app.instruments.audition.getBottom() <= app.instruments.getHeight());
            check("Piano garde rapport en fenetre reduite",
                  std::abs(app.instruments.pianoRect.getWidth() / app.instruments.pianoRect.getHeight() -
                           ratio) < .01f);
            captureView("adaptive-small");
            for (int familyId : {1, 3, 4, 5, 6}) {
                app.instruments.family.setSelectedId(familyId, juce::sendNotificationSync);
                app.instruments.createComponentSnapshot(app.instruments.getLocalBounds());
                auto detail = app.instruments.detailBounds();
                const auto content = fretboardContentBounds(app.shapes[(size_t)app.shapeIndex]);
                check("Diagramme proportionnel famille " + String(familyId),
                      !detail.isEmpty() &&
                          std::abs(detail.getWidth() / content.getWidth() -
                                   detail.getHeight() / content.getHeight()) < .001f &&
                          app.instruments.diagramRect.contains(app.instruments.positionCaptionBounds()));
                captureView("detail-cropped-family-" + String(familyId));
            }
            app.instruments.family.setSelectedId(1, juce::sendNotificationSync);
            app.doc.j = originalDocument;
            app.refresh();
            app.shapeIndex = originalShape;
            // Settings visibility must not reset the lower height or hide its restoration button.
            app.settingsView.setVisible(false);
            app.resized();
            check("Options ferme conserve hauteur valide",
                  app.instruments.getHeight() <= wished &&
                      app.instruments.audition.getBottom() <= app.instruments.getHeight());
            captureView("adaptive-small-no-options");
            app.settingsView.setVisible(true);
            app.resized();
            app.audio.play();
            app.split.fold.onClick();
            check("Repli libere espace et laisse commande accessible",
                  !app.instruments.isVisible() && app.timeline.getHeight() > smallTimeline &&
                      app.split.fold.isVisible() && !app.split.fold.getBounds().isEmpty());
            check("Replier ne coupe pas lecture", app.audio.playing);
            app.savePreferences();
            auto savedLayout = readJson(dataFolder().getChildFile("settings.json"));
            check("Repli et hauteur souhaitee memorises",
                  savedLayout.at("instrumentsHidden") == true && savedLayout.at("instrumentHeight") == 300);
            captureView("adaptive-folded");
            check("Zoom en pied visible avec panneau replie",
                  app.waveZoom.getY() > app.timeline.getY() &&
                      app.readableZoom.getBottom() <= app.split.getY());
            app.setSize(1600, 1200);
            check("Redimensionner ne rouvre pas instruments", !app.instruments.isVisible());
            app.split.fold.onClick();
            check("Restauration hauteur souhaitee",
                  app.instruments.isVisible() && app.instruments.getHeight() == wished);
            check("Deplier ne coupe pas lecture", app.audio.playing);
            app.audio.pause();
            captureView("adaptive-restored-large");
            check("Repli preserve musique et selections", app.doc.j == originalDocument &&
                                                              app.selected == originalSelection &&
                                                              app.shapeIndex == originalShape);
            app.setSize(originalSize.x, originalSize.y);
            app.preferredInstrumentHeight = -1;
            app.savePreferences();
        }
        captureView("splitter-maximum");
        {
            const auto previousSize = app.getBounds();
            const int previousHeight = app.preferredInstrumentHeight;
            app.setSize(1000, 780);
            app.preferredInstrumentHeight = 240;
            app.resized();
            const auto panel = app.instruments.diagramRect;
            const auto content = fretboardContentBounds(app.shapes[(size_t)app.shapeIndex]);
            auto oldArea = panel.reduced(12, 8);
            oldArea.setTop(app.instruments.positionCaptionBounds().getBottom() + 6);
            auto oldFit = [&] {
                const float scale = std::min(oldArea.getWidth() / 140.f, oldArea.getHeight() / 180.f);
                return juce::Rectangle<float>(140 * scale, 180 * scale).withCentre(oldArea.getCentre());
            };
            auto oldBounds = oldFit();
            if (oldBounds.intersects(app.instruments.stringsAudition.getBounds().toFloat().expanded(6))) {
                oldArea.setBottom((float)app.instruments.stringsAudition.getY() - 6);
                oldBounds = oldFit();
            }
            const auto newBounds = app.instruments.detailBounds();
            const float oldScale = oldBounds.getHeight() / 180.f;
            const float newScale = newBounds.getHeight() / content.getHeight();
            check("Gain visible grille petite resolution", newScale >= oldScale * 1.20f);
            const auto freeArea =
                panel.reduced(12, 8).withTop(app.instruments.positionCaptionBounds().getBottom() + 6);
            check("Grille reste centree malgre haut parleur",
                  newBounds.getCentre().getDistanceFrom(freeArea.getCentre()) < .01f);
            check(
                "Seul coin haut parleur reserve",
                !newBounds.intersects(app.instruments.stringsAudition.getBounds().toFloat().expanded(5.9f)));
            writeJson(output.getChildFile("detail-size-gain.json"),
                      {{"window", {1000, 780}},
                       {"previousScale", oldScale},
                       {"currentScale", newScale},
                       {"linearGainPercent", (newScale / oldScale - 1) * 100},
                       {"contentBounds",
                        {content.getX(), content.getY(), content.getWidth(), content.getHeight()}}});
            captureView("detail-small-enlarged");
            app.setSize(previousSize.getWidth(), previousSize.getHeight());
            app.preferredInstrumentHeight = previousHeight;
            app.resized();
        }
        {
            const auto originalSize = app.getBounds();
            const auto originalDoc = app.doc.j;
            const int originalHeight = app.timelinePixels;
            for (bool english : {false, true}) {
                app.setLanguage(english);
                for (auto size : {juce::Point<int>(1000, 780), juce::Point<int>(1400, 940),
                                  juce::Point<int>(2000, 1280)}) {
                    app.setSize(size.x, size.y);
                    for (int requested : {240, 20000}) {
                        app.preferredInstrumentHeight = requested;
                        app.resized();
                        const auto footer = juce::Rectangle<int>(
                            app.timeline.getX(), app.timeline.getBottom() - app.timeline.hintHeight(),
                            app.timeline.getWidth(), app.timeline.hintHeight());
                        for (auto *control :
                             std::initializer_list<juce::Component *>{&app.waveZoom, &app.readableZoom})
                            check("Zoom contenu dans pied avec marge " + String(size.x) + " " +
                                      String(requested),
                                  footer.contains(control->getBounds()) &&
                                      control->getBottom() <= app.split.getY() - 4 &&
                                      control->getY() >= footer.getY() + 4);
                        check("Fleche a droite et barre conservee",
                              app.split.getHeight() == 20 &&
                                  app.split.fold.getRight() == app.split.getWidth() - 4 &&
                                  app.split.fold.getX() > app.split.getWidth() / 2 + 24);
                        const auto caption = app.instruments.positionCaptionBounds();
                        const auto detail = app.instruments.detailBounds();
                        check("Une legende haute sans chevauchement",
                              caption.getHeight() == 22 && caption.getBottom() < detail.getY() &&
                                  app.instruments.diagramRect.contains(detail) &&
                                  !detail.intersects(app.instruments.stringsAudition.getBounds().toFloat()));
                        check("Legende positions traduite",
                              app.instruments.positionCountText() ==
                                  String((int)app.shapes.size()) +
                                      (english ? " available positions — click to change"
                                               : " positions disponibles — cliquer pour changer"));
                        captureView(String("targeted-layout-") + (english ? "en-" : "fr-") + String(size.x) +
                                    "-" + String(requested));
                    }
                    app.split.fold.onClick();
                    check("Zoom hors separateur panneau masque",
                          app.readableZoom.getBottom() <= app.split.getY() - 4 &&
                              app.split.fold.getRight() == app.split.getWidth() - 4);
                    app.split.fold.onClick();
                }
            }
            app.setLanguage(false);
            app.setSize(originalSize.getWidth(), originalSize.getHeight());
            app.timelinePixels = originalHeight;
            app.preferredInstrumentHeight = -1;
            app.resized();
            check("Corrections layout ne changent pas la musique", app.doc.j == originalDoc);
        }
        app.timelineManual = false;
        app.preferredInstrumentHeight = -1;
        app.timelinePixels = 380;
        app.resized();
        check("Affichage ne modifie pas les donnees", app.doc.j == waveDoc);
        // Readability is an initial view, never a lower limit on subsequent navigation.
        {
            const auto bounds = app.getBounds();
            app.setSize(1800, 1100);
            app.timeline.applyReadableZoom();
            const double wideSpan = app.timeline.span;
            const auto documentBefore = app.doc.j;
            app.setSize(1000, 780);
            check("Zoom lisible auto suit reduction largeur",
                  app.timeline.readableZoomActive && app.timeline.span <= wideSpan &&
                      std::abs(app.timeline.span - app.timeline.readableSpan()) < 1e-9);
            app.timeline.zoom(2.);
            const double manualSpan = app.timeline.span;
            app.setSize(1600, 1000);
            check("Zoom manuel respecte apres resize",
                  !app.timeline.readableZoomActive && app.timeline.span == manualSpan);
            app.timeline.applyReadableZoom();
            app.setSize(1200, 900);
            check("Bouton reactive suivi zoom lisible",
                  app.timeline.readableZoomActive &&
                      std::abs(app.timeline.span - app.timeline.readableSpan()) < 1e-9);
            for (double span : {.001, .1, 1., 6., 60., 279., 1000.}) {
                app.timeline.span = span;
                const double pixels = app.timeline.tickStep() * (app.timeline.getWidth() - 70) / span;
                check("Timecodes espaces selon zoom " + String(span), pixels >= 99.);
            }
            check("Adapter zoom conserve donnees", app.doc.j == documentBefore);
            app.setSize(bounds.getWidth(), bounds.getHeight());
            app.timeline.readableZoomActive = false;
        }
        auto addChord = [&](Document &d, double a, double b, String name) {
            d.chords().push_back({{"start", a},
                                  {"end", b},
                                  {"name", utf(name)},
                                  {"notes", name == "N" ? std::vector<int>() : theory.notes(name)}});
        };
        auto reading = playAlongDoc;
        reading.chords().clear();
        reading.j["duration"] = 120.;
        for (int n = 0; n < 30; ++n)
            addChord(reading, n * 4., (n + 1) * 4., n % 2 ? "G7" : "Cm7");
        app.setLoaded(reading, playAlongAudio);
        app.timeline.applyReadableZoom();
        double regularSpan = app.timeline.span;
        check("Zoom initial calcule sur tous les changements", regularSpan > 0 && regularSpan <= 120);
        app.doc.chords().insert(
            app.doc.chords().begin() + 1,
            J{{"start", .01}, {"end", 4.}, {"name", "Dm7"}, {"notes", theory.notes("Dm7")}});
        app.doc.chords()[0]["end"] = .01;
        check("Changement isole bref reste lisible sans exception",
              app.timeline.readableSpan() < regularSpan * .01);
        reading.chords().clear();
        for (int n = 0; n < 120; ++n)
            addChord(reading, n * .3, (n + 1) * .3, n % 2 ? "G7" : "Cm7");
        app.setLoaded(reading, playAlongAudio);
        app.timeline.applyReadableZoom();
        check("Accords rapproches imposent zoom initial plus serre", app.timeline.span < regularSpan);
        app.createComponentSnapshot(app.getLocalBounds());
        double initialRight = -1;
        bool allReadable = true;
        size_t expectedNames = 0;
        for (int n : app.timeline.chordChanges()) {
            auto &c = app.doc.chords()[(size_t)n];
            double at = c.at("start");
            if (text(c, "name") != "N" && at >= app.timeline.offset &&
                app.timeline.x(at) + 100 < app.timeline.getWidth() - 15)
                ++expectedNames;
        }
        for (auto &entry : app.timeline.waveChordLabels) {
            allReadable &= entry.second.getX() >= initialRight + 8;
            initialRight = entry.second.getRight();
        }
        check("Zoom initial rend tous les noms interieurs lisibles",
              allReadable && app.timeline.waveChordLabels.size() >= expectedNames);
        double denseSpan = app.timeline.span;
        app.timeline.zoom(2);
        check("Dezoom libre apres reglage automatique", app.timeline.span > denseSpan);
        app.timeline.span = 9;
        app.timeline.offset = 7;
        auto savedView = app.timeline.viewState();
        auto persisted = initial;
        persisted.j["timeline_view"] = savedView;
        auto viewFile = output.getChildFile("zoom-memorise.knea");
        persisted.save(viewFile);
        app.setLoaded(Document::load(viewFile), originalAudio, viewFile);
        // A two-second test document must clamp a view offset which lies beyond its audio.
        check("Vue projet valide bornee a duree",
              std::abs(app.timeline.span - app.timeline.readableSpan()) < 1e-8 && app.timeline.offset == 0);
        persisted = playAlongDoc;
        persisted.j["timeline_view"] = savedView;
        persisted.save(viewFile);
        app.setLoaded(Document::load(viewFile), playAlongAudio, viewFile);
        check("Reouverture knea recalcule zoom lisible",
              std::abs(app.timeline.span - app.timeline.readableSpan()) < 1e-8 && app.timeline.offset == 0);
        {
            App saveProbe(false);
            auto actualFile = output.getChildFile("vue-sauvee-par-application.knea");
            saveProbe.setLoaded(initial, originalAudio, actualFile);
            saveProbe.timeline.span = 9;
            saveProbe.timeline.offset = 0;
            saveProbe.timeline.audioVisible = false;
            saveProbe.timeline.chordsVisible = false;
            saveProbe.timeline.lyricsVisible = true;
            const auto expectedView = saveProbe.timeline.viewState();
            check("Sauvegarde application conserve etat vue",
                  saveProbe.save() && Document::load(actualFile).j.at("timeline_view") == expectedView);
            auto reopened = Document::load(actualFile);
            saveProbe.setLoaded(reopened, originalAudio, actualFile);
            check("Reouverture conserve pistes et recalcule zoom",
                  !saveProbe.timeline.audioVisible && !saveProbe.timeline.chordsVisible &&
                      saveProbe.timeline.lyricsVisible &&
                      std::abs(saveProbe.timeline.span - saveProbe.timeline.readableSpan()) < 1e-8);
        }
        const auto validView = app.timeline.viewState();
        app.timeline.restoreView(J{{"span", -1}, {"offset", -2}});
        check("Vue invalide ignoree", app.timeline.viewState() == validView);

        auto modes = playAlongDoc;
        modes.chords().clear();
        modes.lyrics().clear();
        addChord(modes, 0, 4, "C");
        addChord(modes, 4, 6, "C");
        addChord(modes, 6, 10, "G");
        addChord(modes, 10, 12, "N");
        addChord(modes, 12, 14, "G");
        addChord(modes, 14, 18, "Am");
        addChord(modes, 18, 24, "F");
        modes.lyrics().push_back(
            {{"start", 8.},
             {"end", 16.},
             {"text", "Stay here tonight"},
             {"words", J::array({{{"start", 8.}, {"end", 10.}, {"text", "Stay"}, {"offset", 0}},
                                 {{"start", 11.}, {"end", 12.}, {"text", "here"}, {"offset", 5}},
                                 {{"start", 14.}, {"end", 16.}, {"text", "tonight"}, {"offset", 10}}})}});
        app.setLoaded(modes, playAlongAudio);
        app.timeline.span = 12;
        app.timeline.position = 1;
        app.timeline.audioVisible = true;
        app.timeline.chordsVisible = true;
        app.timeline.lyricsVisible = true;
        captureView("anticipation-audio");
        check("Indication prochain accord supprimee",
              app.timeline.displayedNextChord.isEmpty() && app.timeline.nextChordBounds.isEmpty());
        const auto anticipationBox = app.timeline.nextChordBounds;
        app.timeline.offset = 8;
        app.timeline.span = 6;
        app.timeline.position = 7;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Anticipation independante du zoom et defilement",
              app.timeline.nextChordBounds.isEmpty() && app.timeline.displayedNextChord.isEmpty());
        app.timeline.position = 10.5;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Pas annonce separee pendant silence", app.timeline.displayedNextChord.isEmpty());
        app.timeline.position = 20;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Pas de prochain accord invente en fin", app.timeline.displayedNextChord.isEmpty());
        app.timeline.audioVisible = false;
        app.timeline.position = 14.5;
        app.timeline.offset = 8;
        app.timeline.span = 12;
        captureView("anticipation-paroles-et-accords");
        check("Anticipation bascule dans paroles", app.timeline.nextChordBounds.isEmpty());
        bool phraseClear = !app.timeline.lyricHits.empty();
        for (auto &hit : app.timeline.lyricHits)
            phraseClear &= !std::get<0>(hit).intersects(app.timeline.nextChordBounds);
        check("Anticipation ne chevauche pas paroles", phraseClear && app.timeline.lyricChordLabels.empty());
        app.timeline.chordsVisible = false;
        captureView("paroles-seules-accords-sur-mots");
        bool anchoredAm = false, orphanG = false, carriedG = false, activeAm = false;
        for (auto item : app.timeline.lyricChordLabels) {
            anchoredAm |= item.index == 5 && item.anchored;
            orphanG |= item.index == 4 && !item.anchored;
            carriedG |= item.index == 2 && !item.anchored;
            activeAm |= item.index == 5 && item.active;
            check("Annotations hors anticipation", !item.bounds.intersects(app.timeline.nextChordBounds));
        }
        check("Accord au dessus du mot horodate", anchoredAm && activeAm);
        check("Accord sans mot reste visible independamment", orphanG && carriedG);
        auto anchored =
            *std::find_if(app.timeline.lyricChordLabels.begin(), app.timeline.lyricChordLabels.end(),
                          [](auto item) { return item.index == 5; });
        auto timedWord = std::find_if(app.timeline.lyricHits.begin(), app.timeline.lyricHits.end(),
                                      [](auto &hit) { return std::get<2>(hit) == "tonight"; });
        check("Placement harmonique au dessus du bon mot",
              timedWord != app.timeline.lyricHits.end() &&
                  anchored.bounds.getX() >= std::get<0>(*timedWord).getX() &&
                  anchored.bounds.getX() < std::get<0>(*timedWord).getRight() &&
                  anchored.bounds.getBottom() < std::get<0>(*timedWord).getCentreY());
        check("Ligne active centree verticalement",
              app.timeline.lyricRows.size() == 1 &&
                  std::abs(app.timeline.lyricRows[0].bounds.getCentreY() -
                           (app.timeline.bands()[2] + 6 + app.timeline.getHeight() -
                            app.timeline.hintHeight() - 3) *
                               .5f) < 2);
        app.timeline.position = 2;
        app.timeline.offset = 0;
        captureView("paroles-seules-introduction");
        check("Accords visibles pendant introduction",
              !app.timeline.lyricChordLabels.empty() &&
                  std::none_of(app.timeline.lyricRows.begin(), app.timeline.lyricRows.end(),
                               [](auto row) { return row.current; }));
        app.timeline.position = 19;
        app.timeline.offset = 16;
        captureView("paroles-seules-instrumental");
        check("Accords visibles sans chant apres phrase",
              std::any_of(app.timeline.lyricChordLabels.begin(), app.timeline.lyricChordLabels.end(),
                          [](auto item) { return item.index == 6 && item.active; }));
        app.timeline.lyricsVisible = false;
        app.timeline.chordsVisible = true;
        app.timeline.position = 14.5;
        captureView("anticipation-accords-seuls");
        bool previewClear = true;
        for (auto &mini : app.timeline.miniBoxes)
            previewClear &= !mini.second.intersects(app.timeline.nextChordBounds);
        check("Anticipation ne recouvre pas les diagrammes",
              previewClear && app.timeline.displayedNextChord.isEmpty());
        check("Anticipation dans piste accords seule", app.timeline.nextChordBounds.isEmpty());
        app.timeline.chordsVisible = false;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Toutes pistes masquees aucune anticipation dessinee", app.timeline.nextChordBounds.isEmpty());
        check("Modes affichage ne modifient pas la musique", app.doc.j == modes.j);
        app.timeline.lyricsVisible = true;
        app.doc.lyrics().clear();
        app.timeline.position = 19;
        captureView("paroles-seules-sans-aucune-parole");
        check("Accords visibles meme sans aucune transcription", !app.timeline.lyricChordLabels.empty());
        auto fullSongLabels = app.timeline.lyricChordLabels;
        check("Accord courant lisible sans transcription",
              std::any_of(fullSongLabels.begin(), fullSongLabels.end(),
                          [](auto item) { return item.index == 6 && item.active; }));
        app.doc.lyrics() = modes.lyrics();
        app.timeline.position = 12.5;
        app.timeline.offset = 8;
        captureView("paroles-seules-changement-sans-mot");
        check("Accord incertain reste visible et actif",
              std::any_of(app.timeline.lyricChordLabels.begin(), app.timeline.lyricChordLabels.end(),
                          [](auto item) { return item.index == 4 && item.active && !item.anchored; }));
        auto isolated =
            std::find_if(app.timeline.lyricChordLabels.begin(), app.timeline.lyricChordLabels.end(),
                         [](auto item) { return item.index == 4; });
        app.timeline.mouseDown(eventFor(app.timeline, isolated->bounds.getCentre(), 1));
        check("Accord autonome garde son temps au clic", std::abs(app.audio.seconds() - 12.) < 1e-8);
        app.doc = modes;
        app.timeline.position = 14.5;
        app.doc.j["transpose"] = 2;
        app.refresh();
        app.createComponentSnapshot(app.getLocalBounds());
        check("Accord annonce respecte transposition", app.timeline.displayedNextChord.isEmpty());
        check("Annotations respectent transposition",
              std::any_of(
                  app.timeline.lyricChordLabels.begin(), app.timeline.lyricChordLabels.end(), [&](auto item) {
                      return item.index == 5 && label(app.doc.chords()[(size_t)item.index], app.doc) == "Bm";
                  }));

        for (int windowHeight : {780, 940}) {
            app.setSize(1200, windowHeight);
            app.timeline.position = 14.5;
            app.timeline.span = 12;
            app.createComponentSnapshot(app.getLocalBounds());
            bool contained = true;
            const auto band = juce::Rectangle<float>(
                55, app.timeline.bands()[2] + 6, app.timeline.getWidth() - 70.f,
                app.timeline.getHeight() - app.timeline.hintHeight() - 9.f - app.timeline.bands()[2]);
            for (auto annotation : app.timeline.lyricChordLabels)
                contained &= band.contains(annotation.bounds);
            check("Annotations lisibles petite fenetre " + String(windowHeight),
                  contained && !app.timeline.lyricChordLabels.empty());
            captureView("paroles-seules-compact-" + String(windowHeight));
        }
        app.setLoaded(modes, playAlongAudio);
        app.setSize(2000, 1280);
        app.timeline.audioVisible = true;
        app.timeline.chordsVisible = true;
        app.timeline.lyricsVisible = true;
        app.preferredInstrumentHeight = -1;
        app.timelinePixels = 500;
        app.resized();
        app.timeline.position = 7;
        captureView("five-cards-current-middle");
        check("Cinq occurrences distinctes dont deux C repetes",
              app.timeline.chordCards.size() == 5 && app.timeline.chordCards.front().first == 0 &&
                  app.timeline.chordCards[2].first == 2);
        auto cardBounds = app.timeline.chordCards;
        app.timeline.zoom(3);
        captureView("five-cards-dezoom");
        check("Zoom onde ne modifie pas les cinq cartes", app.timeline.chordCards == cardBounds);
        auto beforeClick = app.doc.j;
        auto target = app.timeline.chordCards[1];
        app.timeline.mouseDown(eventFor(app.timeline, target.second.getCentre(), 1));
        check("Carte fixe selectionne occurrence exacte",
              app.selected == target.first &&
                  std::abs(app.audio.seconds() -
                           app.doc.chords()[(size_t)target.first].at("start").get<double>()) < 1e-8);
        check("Clic carte ne change pas horodatages", app.doc.j == beforeClick);
        app.timeline.position = 12.5;
        captureView("five-cards-advanced");
        check("Fenetre avance par evenement", app.timeline.chordCards[2].first == 4);
        app.timeline.position = 0;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Debut sans bouclage de cartes",
              app.timeline.chordCards.size() == 3 && app.timeline.chordCards.front().first == 0);
        app.timeline.position = 24;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Fin sans accord invente", app.timeline.chordCards.back().first == 6);
        app.timeline.span = 8;
        app.timeline.follow(8);
        double followed = app.timeline.offset;
        app.timeline.follow(8.04);
        check("Defilement onde continu", std::abs(app.timeline.offset - followed - .04) < 1e-8);
        const juce::Font font(juce::FontOptions("Segoe UI", 26, juce::Font::plain));
        check("Espacement paroles augmente uniquement a affichage",
              Timeline::lyricWidth(font, "He claimed") > Timeline::lyricWidth(font, "Heclaimed"));
        auto unmodified = app.doc.j;
        app.setLanguage(true);
        captureView("english-interface");
        {
            const auto previousSize = app.getBounds();
            for (bool english : {false, true}) {
                app.setLanguage(english);
                for (int width : {1000, 1500}) {
                    app.setSize(width, 1100);
                    captureView(String("readability-") + (english ? "en-" : "fr-") + String(width));
                }
                auto panel = app.settings.createComponentSnapshot(app.settings.getLocalBounds());
                auto file =
                    output.getChildFile(english ? "settings-readable-en.png" : "settings-readable-fr.png")
                        .createOutputStream();
                png.writeImageToStream(panel, *file);
            }
            app.setSize(previousSize.getWidth(), previousSize.getHeight());
            app.setLanguage(true);
        }
        check("Interface anglaise", app.analyseButton.getButtonText() == "Analyze chords" &&
                                        app.newButton.getButtonText() == "New transcription");
        check("Unite de transposition anglaise", app.settings.transpose.getTextValueSuffix() == " semitones");
        check("Statut anglais", !app.status.contains("accords") && !app.status.contains("paroles"));
        check("Localisation ne traduit pas donnees utilisateur", app.doc.j == unmodified);
        app.ownLicense();
        check("Licence propre accessible", app.activeDialog() != nullptr);
        auto *licenseContent = app.activeDialog()->getContentComponent();
        auto *licenseText = dynamic_cast<juce::TextEditor *>(licenseContent->getChildComponent(0));
        check("Licence complete anglaise avec titulaire", licenseText &&
                                                              licenseText->getText().contains("Trucabulles") &&
                                                              licenseText->getText().contains("Article 12"));
        app.activeDialog()->closeButtonPressed();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        app.setLanguage(false);
        check("Retour francais sans redemarrage",
              app.analyseButton.getButtonText() == "Analyser les accords");
        check("Deux manuels livres", resource("manuals/KITHARA-NEA-Manuel-FR.pdf").existsAsFile() &&
                                         resource("manuals/KITHARA-NEA-Manual-EN.pdf").existsAsFile());
        {
            const auto linked = dataFolder().getChildFile("models/whisper/external-medium.json");
            const auto prior = linked.existsAsFile() ? linked.loadFileAsString() : String();
            bool rejected = false;
            try {
                useExistingWhisperMedium(output.getChildFile("recording-alias.wav"));
            } catch (const std::exception &) {
                rejected = true;
            }
            check("Modele local incompatible refuse sans memorisation",
                  rejected && (!linked.existsAsFile() || linked.loadFileAsString() == prior));
            auto external = juce::SystemStats::getEnvironmentVariable("KITHARA_TEST_EXISTING_MEDIUM", {});
            if (external.isNotEmpty()) {
                useExistingWhisperMedium(File(external));
                check("Modele local verifie utilise sans copie", whisperModelFile("medium") == File(external));
                check("Modele local disponible", whisperModelAvailable("medium"));
                auto saved = readJson(linked);
                saved["path"] = utf(output.getChildFile("missing-medium.bin").getFullPathName());
                writeJson(linked, saved);
                check("Modele local disparu demande relocalisation", whisperModelFile("medium") != File(external));
                if (prior.isNotEmpty())
                    linked.replaceWithText(prior);
                else
                    linked.deleteFile();
            }
        }
        if (!whisperModelAvailable("medium")) {
            for (bool english : {false, true}) {
                app.setLanguage(english);
                app.settings.quality.setSelectedId(2, juce::dontSendNotification);
                app.selectWhisperQuality();
                auto *choice = app.activeDialog();
                check("Medium absent propose telechargement",
                      choice && app.settings.quality.getSelectedId() == 1);
                bool source = false, existing = false;
                for (auto *component : choice->getContentComponent()->getChildren())
                    if (auto *link = dynamic_cast<juce::HyperlinkButton *>(component)) {
                        source = link->getURL().toString(false) ==
                                 "https://huggingface.co/ggerganov/whisper.cpp/blob/main/ggml-medium.bin";
                        check("Source secondaire libelle clair",
                              link->getButtonText() == tr("Voir la source du modèle") &&
                                  link->getHeight() == 22 &&
                                  link->findColour(juce::HyperlinkButton::textColourId) != app.theme.bronze);
                    } else if (auto *button = dynamic_cast<juce::TextButton *>(component)) {
                        existing |= button->getButtonText() == tr("Utiliser un modèle existant…");
                        if (button->getButtonText() == tr("Télécharger et installer"))
                            check("Installation action principale",
                                  button->findColour(juce::TextButton::buttonColourId) == app.theme.bronze &&
                                      button->getWidth() >= 210);
                    }
                check("Lien vers source officielle dans proposition", source);
                check("Choix modele local propose dans les deux langues", existing);
                auto image = choice->createComponentSnapshot(choice->getLocalBounds());
                auto stream = output.getChildFile(english ? "local-model-en.png" : "local-model-fr.png").createOutputStream();
                juce::PNGImageFormat().writeImageToStream(image, *stream);
                auto snapshot = choice->createComponentSnapshot(choice->getLocalBounds());
                auto file = output.getChildFile(english ? "medium-choice-en.png" : "medium-choice-fr.png")
                                .createOutputStream();
                png.writeImageToStream(snapshot, *file);
                choice->closeButtonPressed();
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
                check("Refuser Medium conserve Small sans calcul",
                      !app.jobActive && app.settings.quality.getSelectedId() == 1);
            }
            app.setLanguage(false);
        }
        {
            auto windowDoc = playAlongDoc;
            app.setLoaded(windowDoc, playAlongAudio);
            app.timeline.audioVisible = false;
            app.timeline.chordsVisible = false;
            app.timeline.lyricsVisible = true;
            app.setSize(2000, 1600);
            app.timelineManual = true;
            app.preferredInstrumentHeight = -1;
            app.timelinePixels = 700;
            app.resized();
            app.seek(9);
            captureView("lyrics-window-five-lines");
            check("Cinq lignes en grand espace", app.timeline.lyricRows.size() == 5);
            auto current = std::find_if(app.timeline.lyricRows.begin(), app.timeline.lyricRows.end(),
                                        [](auto row) { return row.current; });
            check("Ligne courante milieu cinq lignes", current != app.timeline.lyricRows.end() &&
                                                           current->index == 2 &&
                                                           current == app.timeline.lyricRows.begin() + 2);
            auto before = current->bounds;
            app.timeline.position = 10;
            app.createComponentSnapshot(app.getLocalBounds());
            current = std::find_if(app.timeline.lyricRows.begin(), app.timeline.lyricRows.end(),
                                   [](auto row) { return row.current; });
            check("Ligne reste immobile pendant lecture", current->bounds == before);
            app.seek(13);
            captureView("lyrics-window-next-line");
            current = std::find_if(app.timeline.lyricRows.begin(), app.timeline.lyricRows.end(),
                                   [](auto row) { return row.current; });
            check("Ligne suivante reprend centre", current != app.timeline.lyricRows.end() &&
                                                       current->index == 3 && current->bounds == before);
            app.setSize(1600, 1100);
            app.preferredInstrumentHeight = -1;
            app.timelinePixels = 350;
            app.resized();
            app.seek(9);
            captureView("lyrics-window-three-lines");
            check("Trois lignes espace compact", app.timeline.lyricRows.size() == 3);
            check("Affichage paroles ne modifie pas les donnees", app.doc.j == windowDoc.j);
            app.setSize(1800, 1200);
            app.timelineManual = false;
            app.preferredInstrumentHeight = -1;
            app.timelinePixels = 380;
            app.resized();
        }
        app.setSize(1800, 1200);
        app.setLoaded(playAlongDoc, playAlongAudio);
        app.timeline.audioVisible = true;
        app.timeline.chordsVisible = true;
        app.timeline.lyricsVisible = true;
        app.timeline.span = 12;
        app.seek(9);
        captureView("five-named-chords-play-along");
        check("Cinq grilles nommees autour de accord courant", app.timeline.miniBoxes.size() == 5 &&
                                                                   app.timeline.chordCards[2].first == 2 &&
                                                                   app.selected == 2);
        app.timeline.span = .25;
        app.timeline.offset = 0;
        app.seek(17);
        check("Saut carte ou navigation remet onde dans vue",
              app.timeline.position >= app.timeline.offset &&
                  app.timeline.position <= app.timeline.offset + app.timeline.span);
        // Exercise actual wheel events in both directions on a full-length song.
        auto longDoc = initial;
        longDoc.j["duration"] = 280.;
        app.setLoaded(longDoc, originalAudio);
        app.timeline.span = 30;
        auto wheelEvent = eventFor(app.timeline, {300, 100}, 1);
        juce::MouseWheelDetails wheel;
        wheel.deltaY = -.1f;
        app.timeline.mouseWheelMove(wheelEvent, wheel);
        check("Molette dezoom elargit le temps visible", app.timeline.span > 30);
        wheel.deltaY = .1f;
        app.timeline.mouseWheelMove(wheelEvent, wheel);
        check("Molette zoom inverse", std::abs(app.timeline.span - 30) < 1e-8);
        app.setLoaded(initial, originalAudio);
        const auto beforeZoom = app.doc.j;
        app.timeline.fit();
        check("Vue ajustee conserve la duree du morceau court", app.timeline.span == 2.);
        wheel.deltaY = -.1f;
        app.timeline.mouseWheelMove(wheelEvent, wheel);
        check("Dezoom possible au dela du morceau court", app.timeline.span > 2.);
        for (int n = 0; n < 30; ++n)
            app.timeline.mouseWheelMove(wheelEvent, wheel);
        check("Vue large trente secondes pour morceau court", app.timeline.span == 30.);
        check("Dezoom ne change ni duree ni evenements", app.doc.j == beforeZoom);
        wheel.deltaY = .1f;
        app.timeline.mouseWheelMove(wheelEvent, wheel);
        check("Zoom retour apres vue large", app.timeline.span < 30.);
        for (int n = 0; n < 30; ++n)
            app.timeline.mouseWheelMove(wheelEvent, wheel);
        check("Zoom rapproche reste disponible", app.timeline.span > 0 && app.timeline.span < 1.);
        app.timeline.fit();
        check("Tout voir retrouve le morceau sans espace ajoute",
              app.timeline.span == 2. && app.timeline.offset == 0.);
        app.preferredInstrumentHeight = -1;
        app.timelinePixels = 380;
        app.setSize(1500, 1100);
        auto dialogs = verifyDialogs(app, output);
        check("Dialogues fermes par leur croix", dialogs.size() == 5);
        app.waiting.begin("Analyse en cours");
        auto textBox = app.waiting.titleBounds();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
        check("Texte attente immobile", textBox == app.waiting.titleBounds());
        app.waiting.end();
        check(juce::String::fromUTF8("Projet non modifié par le rendu"), app.doc.chords() == doc.chords());
        app.timeline.span = 2;
        app.createComponentSnapshot(app.timeline.getBounds());
        check("Mini diagramme visible", !app.timeline.miniBoxes.empty());
        auto selectedShape = app.theory.chosen(app.doc.chords()[0], app.doc);
        app.chooseShape();
        check("Selection de position", selectedShape != app.theory.chosen(app.doc.chords()[0], app.doc));
        app.undo();
        app.timeline.span = 200;
        app.createComponentSnapshot(app.getLocalBounds());
        check("Grilles fixes independantes du dezoom", !app.timeline.miniBoxes.empty());
        app.timeline.span = 2;
        std::vector<int> order;
        std::atomic<int> active{0}, maximum{0};
        auto work = [&](int n) {
            return [&, n](Cancel cancel) {
                int count = ++active;
                maximum.store(std::max(maximum.load(), count));
                for (int k = 0; k < 5; ++k) {
                    checkCancel(cancel);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                --active;
                JobResult r;
                r.payload = n;
                return r;
            };
        };
        auto complete = [&](JobResult r) { order.push_back(r.payload.get<int>()); };
        app.runJob("test-1", work(1), complete);
        app.runJob("test-2", work(2), complete);
        auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
        while (app.jobActive && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        check("File sequentielle", order == std::vector<int>({1, 2}) && maximum == 1);
        auto beforeCancel = app.doc.j;
        bool installed = false;
        app.runJob(
            "test-cancel",
            [](Cancel cancel) {
                while (true) {
                    checkCancel(cancel);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                return JobResult();
            },
            [&](JobResult) { installed = true; });
        app.cancel();
        deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
        while (app.jobActive && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        check("Annulation sans resultat partiel", !app.jobActive && !installed && beforeCancel == app.doc.j);
        app.dirty = false;
    }
    J report = {{"passed", true}, {"count", checks.size()}, {"checks", checks}, {"python_runtime", false}};
    writeJson(output.getChildFile("checks.json"), report);
    return report;
}
} // namespace kn
