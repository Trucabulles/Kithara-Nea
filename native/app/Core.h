#pragma once
#include "json.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <thread>
#include <windows.h>
namespace nlohmann {
template <> struct adl_serializer<juce::String> {
    static void to_json(json &j, const juce::String &value) { j = value.toStdString(); }
};
} // namespace nlohmann
namespace kn {
using J = nlohmann::json;
using juce::File;
using juce::String;
inline String str(const std::string &s) { return String::fromUTF8(s.data(), (int)s.size()); }
inline std::string utf(const String &s) { return s.toStdString(); }
inline String text(const J &j, const char *key, const char *fallback = "") {
    return str(j.value(key, std::string(fallback)));
}
inline const J &words(const J &cue) {
    static const J empty = J::array();
    auto it = cue.find("words");
    return it != cue.end() && it->is_array() ? *it : empty;
}
inline void require(bool v, const String &why) {
    if (!v)
        throw std::runtime_error(utf(why));
}
J readJson(const File &);
void writeJson(const File &, const J &);
String tr(const String &);
String tr(const char *);
bool isEnglish();
void languageEnglish(bool);
void translateControls(juce::Component &);
File resources();
File resource(const String &);
File dataFolder();
File profileLocation(const File &applicationFolder, const String &overridePath = {});
String defaultUiLanguage(const File &applicationFolder);
String sha256(const File &);
struct TempFolder {
    File folder;
    TempFolder();
    ~TempFolder();
};
struct Document {
    J j;
    std::shared_ptr<TempFolder> media;
    String mediaMode = "none", audioName;
    Document();
    static Document load(const File &);
    void save(const File &, bool embedded = true) const;
    bool ownsAudio() const;
    void validate() const;
    File audio() const { return File(text(j, "audio")); }
    double duration() const { return j.value("duration", 0.0); }
    J &chords() { return j["chords"]; }
    const J &chords() const { return j.at("chords"); }
    J &lyrics() { return j["lyrics"]; }
    const J &lyrics() const { return j.at("lyrics"); }
};
struct History {
    std::vector<Document> back, forward;
    void push(const Document &);
    Document undo(const Document &);
    Document redo(const Document &);
};
String transpose(String, int);
int pitch(String);
String midiName(int);
int noteMidi(String);
String label(const J &, const Document &);
String shapeKey(const String &, const String &, int);
struct Theory {
    std::map<std::string, std::set<std::string>> aliases;
    J tunings;
    std::map<std::string, std::vector<std::vector<int>>> cache;
    Theory();
    std::vector<std::string> intervals(String) const;
    std::vector<int> notes(String, String bass = {}) const;
    std::vector<std::vector<int>> positions(String, String, int, int limit = 8);
    std::vector<int> chosen(const J &, const Document &);
    String key(const Document &) const;
};
int degree(const std::string &);
int semitone(const std::string &);
struct AudioData {
    std::vector<float> mono;
    double rate = 16000;
    double seconds() const { return mono.size() / rate; }
};
std::vector<float> resample(const std::vector<float> &, int from, int to);
using Cancel = std::function<bool()>;
using Progress = std::function<void(double, String)>;
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error(utf(juce::String::fromUTF8("Analyse annulée"))) {}
};
void checkCancel(const Cancel &);
String runProcess(const juce::StringArray &, Cancel cancel = {}, Progress progress = {},
                  double timeout = 3600);
void checkFile(const File &, const String &expected);
void decodeAudio(const File &, const File &, Cancel cancel = {});
std::shared_ptr<AudioData> loadAudio(const File &, Cancel cancel = {}, int targetRate = 16000);
void saveWave(const File &, const std::vector<float> &, double, int bits = 24);
struct SessionLibrary {
    HMODULE handle = nullptr;
    explicit SessionLibrary(const File &);
    ~SessionLibrary();
    template <class F> F get(const char *name) {
        auto f = GetProcAddress(handle, name);
        require(f != nullptr, "Fonction absente : " + String(name));
        return reinterpret_cast<F>(f);
    }
};
struct Synth {
    std::unique_ptr<SessionLibrary> library;
    void *settings = nullptr;
    void *handle = nullptr;
    std::map<String, int> banks;
    J catalog;
    double rate = 22050;
    Synth();
    ~Synth();
    std::vector<float> chord(std::vector<int>, double, String timbre);
};
class AudioEngine : public juce::AudioIODeviceCallback {
  public:
    juce::AudioDeviceManager devices;
    std::shared_ptr<AudioData> source;
    std::vector<float> previewData;
    double previewRate = 22050, previewPosition = 0, position = 0, outputRate = 48000, speed = 1,
           timeFactor = 1, gain = .7;
    bool playing = false;
    std::optional<std::pair<double, double>> loop;
    juce::CriticalSection lock;
    juce::TimeSliceThread writerThread{"Recording"};
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    File recordingFile;
    double recordingSeconds = 0;
    float inputPeak = 0, recordedPeak = 0;
    double recordingGainDB = 0;
    bool recordingClipped = false;
    bool recordingWriteFailed = false;
    std::array<float, 256> recordingPeaks{};
    size_t recordingPeakIndex = 0;
    AudioEngine();
    ~AudioEngine();
    String initialise(const String &state);
    void setSource(std::shared_ptr<AudioData>);
    void seek(double);
    double seconds();
    void play();
    void pause();
    void stop();
    void preview(std::vector<float>, double);
    void record(const File &);
    void beginRecording(const File &, int inputChannels, double sampleRate);
    File endRecording();
    bool isRecording();
    void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *, int, int,
                                          const juce::AudioIODeviceCallbackContext &) override;
    void audioDeviceAboutToStart(juce::AudioIODevice *) override;
    void audioDeviceStopped() override;
};
void estimateMeter(Document &);
void estimateRhythmActivations(Document &, const std::vector<float> &, const std::vector<float> &,
                               Cancel = {});
std::vector<float> beatFeatures(const AudioData &, Cancel);
Document analyseHarmony(const Document &, Theory &, Cancel, Progress);
File whisperModelFile(String);
bool whisperModelAvailable(String);
File downloadWhisperMedium(Cancel, Progress);
void useExistingWhisperMedium(const File &, Cancel = {});
void analyseBeats(Document &, const AudioData &, Cancel, Progress, const File &diagnostic = {});
using WhisperRunner = std::function<String(const juce::StringArray &, double)>;
J runWhisperCompute(const juce::StringArray &, const File &gpuExecutable, const File &output,
                    String compute, Cancel = {}, const WhisperRunner & = {}, double timeout = 300);
J transcribe(const AudioData &, String model, String language, Cancel, Progress,
             String compute = "auto");
J parseWhisperLyrics(const J &, double duration, bool dtw);
double lyricDisplayTime(double seconds);
double lyricCueEnd(const J &cue);
int activeLyricCue(const J &lyrics, double seconds);
int activeLyricWord(const J &cue, double seconds);
std::pair<J, J> align(String, const J &, Cancel = {});
String initialSheet(const Document &);
void updateAutomaticSheet(Document &, const Document &previous);
void exportMidi(const Document &, const File &);
void exportCsv(const Document &, const File &);
void exportText(const Document &, const File &);
void exportChordPro(const Document &, const File &);
void exportWave(const Document &, Synth &, const File &, Cancel cancel = {});
struct SheetToken {
    String text, chord;
    int sourceStart = 0, sourceEnd = 0;
};
struct SheetLine {
    std::vector<SheetToken> tokens;
};
std::vector<SheetLine> sheetLines(String);
struct PianoVoicing {
    std::vector<int> left, right;
    std::vector<int> notes() const {
        auto result = left;
        result.insert(result.end(), right.begin(), right.end());
        return result;
    }
};
PianoVoicing pianoVoicing(const J &, const Document &, const Theory &);
std::vector<int> stringVoicing(const Document &, const std::vector<int> &);
String stringTimbre(const Document &);
juce::Rectangle<float> fretboardContentBounds(const std::vector<int> &);
void drawFretboard(juce::Graphics &, juce::Rectangle<float>, const std::vector<int> &, bool selected = false,
                   bool fitContent = false);
void drawPiano(juce::Graphics &, juce::Rectangle<float>, const std::vector<int> &,
               const std::vector<int> &left = {}, int pressed = -1, bool showLabels = true);
void exportPdf(const Document &, Theory &, const File &, bool song);
J timedChartLayout(const Document &, Theory &);
void exportTimedChart(const Document &, Theory &, const File &);
J verifyNative(const File &output);
} // namespace kn
