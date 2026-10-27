#pragma once
#include "Core.h"
#include <deque>
namespace kn {
struct Theme : juce::LookAndFeel_V4 {
    juce::Colour paper{0xfff9faf4}, ink{0xff254962}, bronze{0xffad793c}, border{0xffcbbda5},
        button{0xfff1ebdf}, selected{0xffe4d5bb}, control{0xfffffdf6}, hover{0xffe9e0d1};
    void drawPopupMenuItem(juce::Graphics &, const juce::Rectangle<int> &, bool, bool, bool, bool, bool,
                           const String &, const String &, const juce::Drawable *,
                           const juce::Colour *) override;
    juce::Component *getParentComponentForMenuOptions(const juce::PopupMenu::Options &) override;
    void fillResizableWindowBackground(juce::Graphics &, int, int, const juce::BorderSize<int> &,
                                       juce::ResizableWindow &) override;
    void drawResizableWindowBorder(juce::Graphics &, int, int, const juce::BorderSize<int> &,
                                   juce::ResizableWindow &) override;
    juce::AlertWindow *createAlertWindow(const String &, const String &, const String &, const String &,
                                         const String &, juce::MessageBoxIconType, int,
                                         juce::Component *) override;
    void drawAlertBox(juce::Graphics &, juce::AlertWindow &, const juce::Rectangle<int> &,
                      juce::TextLayout &) override;
    void drawDocumentWindowTitleBar(juce::DocumentWindow &, juce::Graphics &, int, int, int, int,
                                    const juce::Image *, bool) override;
    juce::Button *createDocumentWindowButton(int) override;
    Theme();
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font &) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
    juce::Font getLabelFont(juce::Label &) override;
    void drawLabel(juce::Graphics &, juce::Label &) override;
    void drawToggleButton(juce::Graphics &, juce::ToggleButton &, bool, bool) override;
    void drawButtonBackground(juce::Graphics &, juce::Button &, const juce::Colour &, bool, bool) override;
    void drawButtonText(juce::Graphics &, juce::TextButton &, bool, bool) override;
};
class SeekButton : public juce::TextButton {
    bool doublePress = false;

  public:
    std::function<void()> onDoubleClick;
    void mouseDown(const juce::MouseEvent &e) override {
        doublePress = e.getNumberOfClicks() >= 2;
        if (doublePress) {
            if (onDoubleClick)
                onDoubleClick();
        } else
            juce::TextButton::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent &e) override {
        if (doublePress) {
            doublePress = false;
            setState(juce::Button::buttonNormal);
            return;
        }
        juce::TextButton::mouseUp(e);
    }
    void mouseDoubleClick(const juce::MouseEvent &) override {}
};
void roundNativeWindow(juce::Component &);
class CloseableDialog : public juce::DialogWindow {
    bool completed = false;

  public:
    std::function<void(int)> onResult;
    CloseableDialog(String name, juce::Colour colour, bool escape = true)
        : juce::DialogWindow(name, colour, escape) {
        setUsingNativeTitleBar(false);
        setOpaque(false);
    }
    juce::BorderSize<int> getBorderThickness() const override { return juce::BorderSize<int>(12); }
    void resized() override {
        juce::DialogWindow::resized();
        roundNativeWindow(*this);
    }
    void visibilityChanged() override {
        juce::DialogWindow::visibilityChanged();
        roundNativeWindow(*this);
    }
    void exitModalState(int code = 0) {
        if (completed)
            return;
        completed = true;
        juce::Component::exitModalState(code);
        setVisible(false);
        auto cb = onResult;
        if (cb)
            cb(code);
    }
    void activeWindowStatusChanged() override {
        juce::DialogWindow::activeWindowStatusChanged();
        if (auto *b = getCloseButton())
            b->setEnabled(true);
    }
    void closeButtonPressed() override { exitModalState(0); }
    void reopen() {
        completed = false;
        setVisible(true);
    }
    bool keyPressed(const juce::KeyPress &key) override {
        if (key == juce::KeyPress::escapeKey) {
            closeButtonPressed();
            return true;
        }
        return juce::DialogWindow::keyPressed(key);
    }
};
class DataWindow;
class App;
class Timeline : public juce::Component {
    App &app;
    struct WaveTile {
        int64_t index;
        juce::Image image;
    };
    struct Peak {
        float low = 0, high = 0;
    };
    std::deque<WaveTile> waveTiles;
    std::vector<std::vector<Peak>> wavePeaks;
    const AudioData *cacheSource = nullptr;
    double cacheDensity = -1;
    int cacheHeight = 0;
    float renderScale = 1;
    std::unique_ptr<juce::VBlankAttachment> animation;
    Peak peakRange(int64_t first, int64_t last) const;
    void paintWaveform(juce::Graphics &, int bottom);
    void animate();
    String drag;
    double dragAt = 0, dragOffset = 0;
    int dragIndex = -1, pressedCard = -1;

  public:
    double offset = 0, span = 30, position = 0;
    bool audioVisible = true, chordsVisible = true, lyricsVisible = true;
    std::vector<std::pair<int, juce::Rectangle<float>>> miniBoxes, chordCards;
    int currentChord() const;
    void paintChordCards(juce::Graphics &);
    void follow(double seconds);
    void prepareWaveform();
    void refreshPlayback(double seconds);
    size_t waveformBytes() const;
    std::vector<juce::Rectangle<int>> playbackRegions;
    uint64_t wavePeakBuilds = 0, waveTileBuilds = 0;
    static constexpr size_t waveCacheLimit = 24 * 1024 * 1024;

    static float lyricWidth(const juce::Font &, const String &);
    std::vector<std::tuple<juce::Rectangle<float>, double, String>> lyricHits;
    std::vector<std::pair<double, juce::Rectangle<float>>> waveChordLabels;
    std::vector<double> waveChordMarkers;
    struct LyricChord {
        int index;
        juce::Rectangle<float> bounds;
        bool anchored, active;
    };
    std::vector<LyricChord> lyricChordLabels;
    juce::Rectangle<float> nextChordBounds;
    String displayedNextChord;
    explicit Timeline(App &);
    std::vector<int> chordChanges() const;
    double readableSpan() const;
    void applyReadableZoom();
    J viewState() const;
    void restoreView(const J &);
    bool readableZoomActive = true;
    int readableWidth = 0;
    void resized() override;
    double tickStep() const;
    void paintLyricChords(juce::Graphics &, const J *, juce::Rectangle<float>, float textOrigin);
    void paintLyricsWindow(juce::Graphics &, juce::Rectangle<float>);
    struct LyricRow {
        int index;
        juce::Rectangle<float> bounds;
        bool current;
    };
    std::vector<LyricRow> lyricRows;
    const J *displayLyricCue = nullptr;
    float x(double) const;
    double time(float) const;
    std::array<float, 3> bands() const;
    String hintText() const;
    int hintTextHeight() const;
    int hintHeight() const;
    float chordFontHeight(float visibleWidth = 500.f) const;
    float lyricFontHeight() const;
    void fit();
    void zoom(double, double anchor = -1);
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
    void mouseDoubleClick(const juce::MouseEvent &) override;
};
class EventsTable : public juce::Component, public juce::TableListBoxModel {
    App &app;
    bool lyrics;
    bool selecting = false;

  public:
    juce::TableListBox table;
    EventsTable(App &, bool);
    int getNumRows() override;
    void paintRowBackground(juce::Graphics &, int, int, int, bool) override;
    void paintCell(juce::Graphics &, int, int, int, int, bool) override;
    juce::Component *refreshComponentForCell(int, int, bool, juce::Component *) override;
    void selectedRowsChanged(int) override;
    void cellClicked(int, int, const juce::MouseEvent &) override;
    void cellDoubleClicked(int, int, const juce::MouseEvent &) override;
    void resized() override;
    void update();
    void select(int, bool scroll = true);
};
class InstrumentView : public juce::Component, public juce::TooltipClient {
    friend J verifyNative(const File &);
    App &app;
    std::vector<std::pair<int, juce::Rectangle<float>>> pianoKeys;
    int pressedKey = -1;
    void pressKeyAt(juce::Point<float>);

  public:
    juce::ComboBox family;
    juce::Label familyCaption;
    juce::TextButton audition, stringsAudition;
    juce::Rectangle<float> diagramRect, pianoRect;
    juce::AffineTransform pianoTransform;
    juce::Rectangle<float> pianoReference{0, 0, 1015, 162};
    juce::Rectangle<float> detailBounds() const;
    juce::Rectangle<float> chordNameBounds() const;
    juce::Rectangle<float> positionCaptionBounds() const;
    String positionCountText() const;
    String getTooltip() override;
    explicit InstrumentView(App &);
    void resized() override;
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    int highlightedKey() const { return pressedKey; }
};
class SettingsPanel : public juce::Component {
    App &app;
    std::vector<std::unique_ptr<juce::Label>> labels;
    void caption(String, juce::Component &);

  public:
    juce::ToggleButton beats{tr("Repérer les pulsations et le tempo")};
    juce::Slider transpose, capo, bpm, meter, speed;
    juce::ComboBox signature;
    juce::Label keyFrom, keyTo, rhythmInfo;
    juce::ComboBox tuning, quality, language, compute;
    juce::TextButton adapt{tr("Appliquer à l’écoute")}, transcribeButton{tr("Transcrire les paroles")},
        alignButton{tr("Saisir / aligner les paroles…")}, reportButton{tr("Vérifier l’alignement…")};
    explicit SettingsPanel(App &);
    void refresh();
    void resized() override;
    void paint(juce::Graphics &) override;
};
class WaitCard : public juce::Component, private juce::Timer {
    App &app;
    double started = 0;
    int dots = 0;
    String title;
    void timerCallback() override;

  public:
    juce::Rectangle<int> titleBounds() const;
    explicit WaitCard(App &);
    void begin(String);
    void end();
    void paint(juce::Graphics &) override;
    bool keyPressed(const juce::KeyPress &) override;
};
class SplitHandle : public juce::Component {
    App &app;
    int start = 0;

  public:
    juce::TextButton fold;
    explicit SplitHandle(App &);
    void resized() override;
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
};
struct JobResult {
    std::optional<Document> document;
    std::shared_ptr<AudioData> audio;
    J payload;
};
class App : public juce::Component, private juce::Timer {
    friend J verifyNative(const File &);
    void timerCallback() override;
    void finished(std::shared_ptr<JobResult>, String, std::function<void(JobResult)>);
    std::thread worker;
    struct Queued {
        String caption;
        std::function<JobResult(Cancel)> work;
        std::function<void(JobResult)> complete;
    };
    std::deque<Queued> queue;
    juce::ComponentDragger windowDrag;
    bool closing = false;
    std::function<void()> closeCallback;
    juce::Point<int> storedChordScroll, storedLyricScroll;
    int storedTab = 0;
    std::unique_ptr<DataWindow> dataWindow;
    std::vector<std::unique_ptr<CloseableDialog>> dialogs;
    juce::LookAndFeel *previousLook = nullptr;

  public:
    Theme theme;
    Document doc;
    History history;
    Theory theory;
    AudioEngine audio;
    std::unique_ptr<Synth> synth;
    J preferences;
    File project;
    std::shared_ptr<AudioData> source;
    juce::Image logo;
    bool dirty = false, filling = false, jobActive = false, detailsOpen = true, extraVisual = true;
    bool timelineManual = false, instrumentsHidden = false;
    int preferredInstrumentHeight = -1;
    double recordingDeadline = 0;
    void beginRecordingNow();
    void cancelRecordingCountdown();
    std::atomic<bool> cancelFlag{false};
    std::atomic<double> jobProgress{-1};
    void selectWhisperQuality();
    int selected = -1, timelinePixels = 280;
    String status = tr("Ouvrir un audio ou un projet."), caption;
    std::vector<std::vector<int>> shapes;
    int shapeIndex = 0;
    Timeline timeline;
    SplitHandle split;
    EventsTable chordTable, lyricTable;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    InstrumentView instruments;
    SettingsPanel settings;
    juce::Viewport settingsView;
    WaitCard waiting;
    juce::Label title, clock, device, statusLabel, filename, volumeLabel, inputGainLabel;
    juce::Slider volume, waveZoom, inputGain;
    juce::TextButton readableZoom{tr("Zoom lisible")};
    void syncZoom();
    void toggleInstruments();
    void setLanguage(bool english);
    void ownLicense();
    void manual();
    SeekButton begin, end;
    juce::TextButton menu, newButton{tr("Nouvelle transcription")}, analyseButton{tr("Analyser les accords")},
        exportButton{tr("Exporter…")}, back{juce::String::fromUTF8("‹ 5 s")}, play{tr("Lecture")}, stop,
        forward{juce::String::fromUTF8("5 s ›")}, record, options{tr("Options")}, loop{tr("Boucle")},
        tracks{tr("Pistes ▾")}, details, help, windowMin, windowMax, windowClose;
    juce::Component::SafePointer<App> safePointer() { return this; }
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseDoubleClick(const juce::MouseEvent &) override;
    explicit App(bool audioEnabled = true);
    ~App() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress &) override;
    void refresh(bool preserveView = true);
    void modified();
    void snapshot();
    void select(int, bool scroll = true);
    void seek(double);
    void moveBoundary(int, double);
    void chooseShape();
    void audition();
    void auditionNotes(std::vector<int>, String timbre = "upright");
    void auditionStrings();
    void changeArrangement();
    void changeFamily();
    void toggleDetails();
    void editTime(int row, bool lyrics);
    bool applyTimes(int row, bool lyrics, double start, double end, String &error);
    bool applyDataCell(int row, bool lyrics, int column, String value, String &error);
    bool applyWordCell(int row, int word, int column, String value, String &error);
    void editWords(int row);
    DataWindow *editingWindow() const { return dataWindow.get(); }
    void undo();
    void redo();
    void menuPopup();
    void exportPopup(juce::Component *origin = nullptr);
    void newProject();
    void chordPopup(int);
    void editChord();
    void splitChord();
    void mergeChord();
    void clearChord();
    void editLyric(int);
    void marker();
    void rememberProject();
    void openAudio(File = {}, bool recorded = false);
    void openProject(File = {});
    bool save(bool saveAs = false);
    void saveLight();
    bool confirmChanges();
    void analyse();
    void startChordAnalysis();
    void finishChordAnalysis(JobResult);
    void transcribeLyrics();
    void alignLyrics();
    void acceptReference(String, int);
    void finishAlignment(JobResult);
    CloseableDialog *presentDialog(String, std::unique_ptr<juce::Component>, int, int,
                                   std::function<void(int, juce::Component &)> completed = {});
    CloseableDialog *activeDialog() const;
    void alignmentReport();
    void editSheet();
    void exportFile(String);
    void audioSettings();
    void about();
    void licenses();
    void shortcuts();
    void recordAudio();
    void applyPlayback();
    void cancel();
    void requestClose(std::function<void()>);
    void runJob(String, std::function<JobResult(Cancel)>, std::function<void(JobResult)>);
    void setLoaded(Document, std::shared_ptr<AudioData>, File projectFile = {});
    void setStatus(String);
    void savePreferences();
    void resetView();
};
class DataWindow : public CloseableDialog {
    App &app;

  public:
    explicit DataWindow(App &);
    ~DataWindow() override;
    bool keyPressed(const juce::KeyPress &) override;
};
J verifyDisplay(App &, const File &);
J verifyDialogs(App &, const File &);
void verifySheetEditor(App &, const File &);
} // namespace kn
