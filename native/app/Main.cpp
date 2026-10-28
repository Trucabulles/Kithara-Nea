#include "UI.h"
#include <dwmapi.h>
namespace kn {
class Application : public juce::JUCEApplication {
    struct Window : juce::DocumentWindow {
        Window(bool audioEnabled = true)
            : DocumentWindow(juce::String::fromUTF8("KITHÁRA NÉA"), juce::Colour(0xfff9faf4), allButtons) {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0);
            setContentOwned(new App(audioEnabled), true);
            setResizable(true, false);
            auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            setResizeLimits(std::min(1000, area.getWidth()), std::min(780, area.getHeight()), 8192, 8192);
            centreWithSize(std::min(1400, area.getWidth()), std::min(940, area.getHeight()));
            setVisible(true);
            getContentComponent()->grabKeyboardFocus();
            if (auto *peer = getPeer()) {
                int corners = 2;
                DwmSetWindowAttribute((HWND)peer->getNativeHandle(), 33, &corners, sizeof(corners));
                auto engines = peer->getAvailableRenderingEngines();
                for (int i = 0; i < engines.size(); ++i)
                    if (engines[i].containsIgnoreCase("software")) {
                        peer->setCurrentRenderingEngine(i);
                        break;
                    }
            }
        }
        bool keyPressed(const juce::KeyPress &key) override {
            return static_cast<App *>(getContentComponent())->keyPressed(key);
        }
        void closeButtonPressed() override {
            static_cast<App *>(getContentComponent())->requestClose([] {
                juce::JUCEApplication::getInstance()->quit();
            });
        }
    };
    std::unique_ptr<Window> window;

  public:
    const String getApplicationName() override { return juce::String::fromUTF8("KITHÁRA NÉA"); }
    const String getApplicationVersion() override { return "0.2.2"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const String &command) override {
        auto args = juce::StringArray::fromTokens(command, true);
        for (auto &a : args)
            a = a.unquoted();
        try {
            if (args.size() >= 2 && args[0] == "--verify-display") {
                File report(args[1]);
                report.createDirectory();
                SetEnvironmentVariableW(L"KITHARA_PROFILE",
                                        report.getChildFile("profile").getFullPathName().toWideCharPointer());
                window = std::make_unique<Window>(false);
                verifyDisplay(*static_cast<App *>(window->getContentComponent()), report);
                window.reset();
                quit();
                return;
            }
            if (args.size() >= 2 && args[0] == "--verify-download") {
                File report(args[1]);
                report.createDirectory();
                SetEnvironmentVariableW(L"KITHARA_PROFILE",
                                        report.getChildFile("profile").getFullPathName().toWideCharPointer());
                bool cancel = false, clean = false;
                double reached = 0;
                try {
                    downloadWhisperMedium([&] { return cancel; },
                                          [&](double progress, String) {
                                              reached = progress;
                                              if (progress > .00008)
                                                  cancel = true;
                                          });
                } catch (const Cancelled &) {
                    clean = true;
                }
                auto folder = dataFolder().getChildFile("models/whisper");
                bool installed = folder.getChildFile("ggml-medium.bin").existsAsFile();
                auto partials = folder.findChildFiles(File::findFiles, false, "*.part");
                writeJson(report.getChildFile("download-check.json"),
                          {{"cancelledCleanly", clean},
                           {"installed", installed},
                           {"partialFiles", partials.size()},
                           {"receivedBeforeCancellation", (int64_t)(reached / .97 * 1533763059.)}});
                require(clean && !installed && partials.isEmpty(), "Annulation du telechargement invalide");
                quit();
                return;
            }
            if (args.size() >= 2 && args[0] == "--verify-recording") {
                File output(args[1]);
                output.createDirectory();
                AudioEngine engine;
                const auto error = engine.initialise({});
                require(error.isEmpty(), error);
                const auto file = output.getChildFile("microphone.wav");
                engine.record(file);
                const auto start = juce::Time::getMillisecondCounterHiRes();
                while (juce::Time::getMillisecondCounterHiRes() - start < 6000)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
                engine.endRecording();
                auto signal = loadAudio(file, {}, (int)engine.outputRate);
                double energy = 0;
                for (auto value : signal->mono)
                    energy += value * value;
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                engine.devices.getAudioDeviceSetup(setup);
                writeJson(output.getChildFile("recording.json"),
                          {{"input", utf(setup.inputDeviceName)}, {"output", utf(setup.outputDeviceName)},
                           {"sampleRate", engine.outputRate}, {"seconds", signal->seconds()},
                           {"peak", engine.recordedPeak}, {"rms", std::sqrt(energy / signal->mono.size())},
                           {"writeFailed", engine.recordingWriteFailed}, {"bytes", file.getSize()}});
                quit();
                return;
            }
            if (args.size() >= 2 && args[0] == "--verify-native") {
                verifyNative(File(args[1]));
                quit();
                return;
            }
            if (args.size() >= 3 && args[0] == "--decode-rhythm") {
                File output(args[2]);
                output.getParentDirectory().createDirectory();
                auto raw = readJson(File(args[1]));
                Document document;
                estimateRhythmActivations(document, raw.at("beatProbability").get<std::vector<float>>(),
                                          raw.at("downbeatProbability").get<std::vector<float>>());
                writeJson(output, document.j);
                quit();
                return;
            }
            if (args.size() >= 3 && args[0] == "--beats-native") {
                File output(args[2]);
                output.getParentDirectory().createDirectory();
                auto data = loadAudio(File(args[1]));
                Document document;
                document.j["duration"] = data->seconds();
                document.j["audio"] = utf(File(args[1]).getFullPathName());
                analyseBeats(document, *data, {}, {}, output);
                quit();
                return;
            }
            if (args.size() >= 3 && args[0] == "--transcribe-native") {
                auto audio = loadAudio(File(args[1]));
                auto result = transcribe(*audio, args.size() > 3 ? args[3] : String("small"), "auto", {}, {},
                                         args.size() > 4 ? args[4] : String("auto"));
                writeJson(File(args[2]), result);
                quit();
                return;
            }
            if (args.size() >= 4 && args[0] == "--parse-whisper-dtw") {
                const double duration = args[3].getDoubleValue();
                auto result = parseWhisperLyrics(readJson(File(args[1])), duration,
                                                 args.size() < 5 || args[4] != "legacy");
                Document check;
                check.j["duration"] = duration;
                check.lyrics() = result.at("lyrics");
                check.validate();
                writeJson(File(args[2]), result);
                quit();
                return;
            }
            if (args.size() >= 3 && args[0] == "--roundtrip-native") {
                auto doc = Document::load(File(args[1]));
                doc.save(File(args[2]));
                quit();
                return;
            }
            if (args.size() >= 3 && args[0] == "--chart-native") {
                auto document = Document::load(File(args[1]));
                Theory theory;
                exportTimedChart(document, theory, File(args[2]));
                writeJson(File(args[2] + ".layout.json"), timedChartLayout(document, theory));
                quit();
                return;
            }
            if (args.size() >= 3 && args[0] == "--analyse-native") {
                auto data = loadAudio(File(args[1]));
                Document input;
                input.j["audio"] = utf(File(args[1]).getFullPathName());
                input.j["duration"] = data->seconds();
                Theory theory;
                auto result = analyseHarmony(input, theory, {}, {});
                analyseBeats(result, *data, {}, {});
                writeJson(File(args[2]), result.j);
                quit();
                return;
            }
            window = std::make_unique<Window>();
            if (args.size() && File(args[0]).existsAsFile()) {
                auto *app = static_cast<App *>(window->getContentComponent());
                if (File(args[0]).hasFileExtension("knea;json"))
                    app->openProject(File(args[0]));
                else
                    app->openAudio(File(args[0]));
            }
        } catch (const std::exception &e) {
            auto path = dataFolder().getChildFile("erreur-native.txt");
            path.replaceWithText(String::fromUTF8(e.what()));
            if (args.size() && args[0].startsWith("--")) {
                setApplicationReturnValue(1);
                quit();
            } else
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       juce::String::fromUTF8("KITHÁRA NÉA"),
                                                       String::fromUTF8(e.what()));
        }
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override {
        if (window)
            window->closeButtonPressed();
        else
            quit();
    }
    void anotherInstanceStarted(const String &) override {}
};
} // namespace kn
START_JUCE_APPLICATION(kn::Application)
