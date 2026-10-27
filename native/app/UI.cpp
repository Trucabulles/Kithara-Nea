#include "UI.h"
namespace kn {
static juce::DocumentWindow *parentWindow(juce::Component *c) {
    return c->findParentComponentOfClass<juce::DocumentWindow>();
}
static void message(App &app, String title, String body) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, tr(title), tr(body), "OK",
                                           &app);
}
static void textDialog(App &app, String title, String hint, String value, bool canAlign = false,
                       bool readOnly = false, std::function<void(int, String)> completed = {}) {
    struct Content : juce::Component {
        juce::Label hint;
        juce::TextEditor editor;
        juce::TextButton save{tr("Conserver le texte")}, action{tr("Aligner sur l’audio")},
            cancel{tr("Annuler")};
        Content(String h, String text, bool align, bool readOnly) {
            addAndMakeVisible(hint);
            hint.setText(h, juce::dontSendNotification);
            hint.setFont(juce::FontOptions(15));
            addAndMakeVisible(editor);
            editor.setMultiLine(true, true);
            editor.setReturnKeyStartsNewLine(true);
            editor.setFont(juce::FontOptions(17));
            editor.setText(text, false);
            editor.setReadOnly(readOnly);
            if (readOnly)
                save.setButtonText(tr("Fermer"));
            for (auto *b : {&save, &action, &cancel})
                addAndMakeVisible(*b);
            action.setVisible(align);
            cancel.setVisible(!readOnly);
            setSize(820, 600);
        }
        void resized() override {
            auto r = getLocalBounds().reduced(20);
            hint.setBounds(r.removeFromTop(62));
            auto buttons = r.removeFromBottom(38);
            cancel.setBounds(buttons.removeFromRight(100));
            buttons.removeFromRight(10);
            action.setBounds(buttons.removeFromRight(175));
            buttons.removeFromRight(10);
            save.setBounds(buttons.removeFromRight(175));
            r.removeFromBottom(12);
            editor.setBounds(r);
        }
    };
    auto content = std::make_unique<Content>(hint, value, canAlign, readOnly);
    auto *controls = content.get();
    auto *dialog = app.presentDialog(
        title, std::move(content), 860, 650, [completed](int code, juce::Component &component) {
            if (completed)
                completed(code, static_cast<Content &>(component).editor.getText());
        });
    auto safe = juce::Component::SafePointer<CloseableDialog>(dialog);
    controls->save.onClick = [safe] {
        if (safe)
            safe->exitModalState(1);
    };
    controls->action.onClick = [safe] {
        if (safe)
            safe->exitModalState(2);
    };
    controls->cancel.onClick = [safe] {
        if (safe)
            safe->exitModalState(0);
    };
}

App::App(bool audioEnabled)
    : timeline(*this), split(*this), chordTable(*this, false), lyricTable(*this, true), instruments(*this),
      settings(*this), waiting(*this) {
    previousLook = &juce::LookAndFeel::getDefaultLookAndFeel();
    juce::LookAndFeel::setDefaultLookAndFeel(&theme);
    setLookAndFeel(&theme);
    setWantsKeyboardFocus(true);
    try {
        auto file = dataFolder().getChildFile("settings.json");
        preferences = file.existsAsFile() ? readJson(file) : J::object();
    } catch (...) {
        preferences = J::object();
    }
    languageEnglish(preferences.value("uiLanguage", utf(defaultUiLanguage(
                         File::getSpecialLocation(File::currentExecutableFile).getParentDirectory()))) == "en");
    detailsOpen = preferences.value("details", true);
    extraVisual = preferences.value("extraVisual", true);
    timelinePixels = preferences.value("timelineHeight", 280);
    timelineManual = preferences.value("timelineManual", false);
    instrumentsHidden = preferences.value("instrumentsHidden", false);
    preferredInstrumentHeight = preferences.value("instrumentHeight", -1);
    timeline.audioVisible = preferences.value("audioVisible", true);
    timeline.chordsVisible = preferences.value("chordsVisible", true);
    timeline.lyricsVisible = preferences.value("lyricsVisible", true);
    logo = juce::ImageFileFormat::loadFrom(resource("independent/brand/logo.png"));
    for (auto *c : std::initializer_list<juce::Component *>{
             &timeline,     &split,    &instruments, &settingsView, &title,     &clock,      &device,
             &statusLabel,  &filename, &volume,      &volumeLabel,  &menu,      &newButton,  &analyseButton,
             &exportButton, &begin,    &play,        &stop,         &end,       &record,     &loop,
             &tracks,       &details,  &help,        &windowMin,    &windowMax, &windowClose})
        addAndMakeVisible(c);
    addAndMakeVisible(options);
    addAndMakeVisible(inputGainLabel);
    addAndMakeVisible(inputGain);
    inputGainLabel.setText(tr("Gain d’entrée"), juce::dontSendNotification);
    inputGain.setRange(0, 24, 1);
    inputGain.setSliderStyle(juce::Slider::LinearHorizontal);
    inputGain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 24);
    inputGain.setTextValueSuffix(" dB");
    inputGain.setTooltip(tr("Gain appliqué uniquement aux nouvelles prises audio"));
    inputGain.onValueChange = [this] {
        juce::ScopedLock lock(audio.lock);
        audio.recordingGainDB = inputGain.getValue();
    };
    inputGain.setValue(std::clamp(preferences.value("recordingGainDB", 0.), 0., 24.));
    addAndMakeVisible(waveZoom);
    addAndMakeVisible(readableZoom);
    waveZoom.setRange(0, 1);
    waveZoom.setSliderStyle(juce::Slider::LinearHorizontal);
    waveZoom.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    waveZoom.setTooltip(tr("Zoom de la forme d’onde"));
    waveZoom.onValueChange = [this] {
        double maximum = std::max(30., doc.duration()), minimum = std::min(.001, timeline.readableSpan());
        double target = maximum * std::pow(minimum / maximum, waveZoom.getValue());
        timeline.zoom(target / timeline.span);
    };
    readableZoom.onClick = [this] { timeline.applyReadableZoom(); };
    readableZoom.setTooltip(tr("Retrouver le zoom lisible pour tous les accords"));
    settings.addAndMakeVisible(details);
    addChildComponent(waiting);
    settingsView.setViewedComponent(&settings, false);
    settingsView.setScrollBarsShown(true, false);
    tabs.addTab(tr("Accords"), theme.paper, &chordTable, false);
    tabs.addTab(tr("Paroles horodatées"), theme.paper, &lyricTable, false);
    tabs.setCurrentTabIndex(std::clamp(preferences.value("tab", 0), 0, 1), false);
    settingsView.setVisible(preferences.value("settingsVisible", true));
    menu.setComponentID("file");
    begin.setComponentID("begin");
    end.setComponentID("end");
    stop.setComponentID("stop");
    play.setComponentID("play");
    loop.setComponentID("loop");
    play.setTooltip(tr("Lecture / pause"));
    loop.setTooltip(tr("Activer / désactiver la boucle"));
    record.setComponentID("record");
    help.setComponentID("help");
    windowMin.setComponentID("windowMin");
    windowMax.setComponentID("windowMax");
    windowClose.setComponentID("windowClose");
    analyseButton.setColour(juce::TextButton::buttonColourId, theme.bronze);
    analyseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    title.setFont(juce::FontOptions("Segoe UI", 18, juce::Font::bold));
    title.setJustificationType(juce::Justification::centred);
    title.setInterceptsMouseClicks(false, false);
    clock.setFont(juce::FontOptions(16));
    device.setFont(juce::FontOptions(16));
    statusLabel.setFont(juce::FontOptions(16));
    filename.setFont(juce::FontOptions(16));
    volumeLabel.setFont(juce::FontOptions(16));
    volume.setRange(0, 100, 1);
    volume.setSliderStyle(juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volume.setValue(preferences.value("volume", 70.), juce::dontSendNotification);
    volume.onValueChange = [this] {
        juce::ScopedLock guard(audio.lock);
        audio.gain = volume.getValue() / 100;
        volumeLabel.setText("Volume  " + String((int)volume.getValue()) + " %", juce::dontSendNotification);
    };
    volume.onValueChange();
    menu.onClick = [this] { menuPopup(); };
    newButton.onClick = [this] { openAudio(); };
    analyseButton.onClick = [this] { analyse(); };
    exportButton.onClick = [this] { exportPopup(); };
    begin.onClick = [this] { seek(audio.seconds() - 5); };
    begin.onDoubleClick = [this] {
        timeline.offset = 0;
        seek(0);
    };
    begin.setTooltip(tr("Reculer de 5 secondes · Double-clic : début"));

    end.onClick = [this] { seek(audio.seconds() + 5); };
    end.onDoubleClick = [this] {
        seek(doc.duration());
        timeline.offset = std::max(0., doc.duration() - timeline.span);
        timeline.repaint();
    };
    end.setTooltip(tr("Avancer de 5 secondes · Double-clic : fin"));

    play.onClick = [this] {
        if (recordingDeadline > 0) {
            cancelRecordingCountdown();
            return;
        }
        if (audio.isRecording()) {
            recordAudio();
            return;
        }
        juce::ScopedLock guard(audio.lock);
        if (audio.playing) {
            audio.pause();
            timeline.position = audio.seconds();
            timeline.repaint();
        } else
            audio.play();
    };
    stop.onClick = [this] {
        if (recordingDeadline > 0) {
            cancelRecordingCountdown();
            return;
        }
        if (audio.isRecording())
            recordAudio();
        else {
            audio.stop();
            timeline.position = 0;
            timeline.repaint();
        }
    };
    record.onClick = [this] { recordAudio(); };
    loop.setClickingTogglesState(true);
    loop.onClick = [this] {
        juce::ScopedLock guard(audio.lock);
        if (!loop.getToggleState())
            audio.loop.reset();
        else if (!audio.loop)
            audio.loop = {{0., doc.duration()}};
        timeline.repaint();
    };
    tracks.onClick = [this] {
        juce::PopupMenu m;
        m.setLookAndFeel(&theme);
        m.addItem(1, tr("Forme d’onde"), true, timeline.audioVisible);
        m.addItem(2, tr("Accords"), true, timeline.chordsVisible);
        m.addItem(3, tr("Paroles"), true, timeline.lyricsVisible);
        m.addSeparator();
        m.addItem(4, tr("Tout voir"));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(tracks),
                        [safe = safePointer()](int id) {
                            if (!safe)
                                return;
                            if (id == 1)
                                safe->timeline.audioVisible = !safe->timeline.audioVisible;
                            if (id == 2)
                                safe->timeline.chordsVisible = !safe->timeline.chordsVisible;
                            if (id == 3)
                                safe->timeline.lyricsVisible = !safe->timeline.lyricsVisible;
                            if (id == 4)
                                safe->timeline.fit();
                            safe->timeline.repaint();
                        });
    };
    options.onClick = [this] {
        settingsView.setVisible(!settingsView.isVisible());
        resized();
    };
    details.onClick = [this] { toggleDetails(); };
    help.onClick = [this] {
        juce::PopupMenu m;
        m.setLookAndFeel(&theme);
        m.addItem(1, tr("Raccourcis clavier"));
        m.addItem(2, tr("À propos"));
        m.addItem(3, tr("Crédits et licences tierces"));
        m.addItem(4, tr("Manuel d’utilisation"));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(help), [safe = safePointer()](int id) {
            if (!safe)
                return;
            if (id == 1)
                safe->shortcuts();
            if (id == 2)
                safe->about();
            if (id == 3)
                safe->licenses();
            if (id == 4)
                safe->manual();
        });
    };
    windowMin.onClick = [this] {
        if (auto *w = parentWindow(this))
            w->setMinimised(true);
    };
    windowMax.onClick = [this] {
        if (auto *w = parentWindow(this))
            w->setFullScreen(!w->isFullScreen());
    };
    windowClose.onClick = [this] {
        if (auto *w = parentWindow(this))
            w->closeButtonPressed();
    };
    setSize(1400, 940);
    refresh(false);
    if (audioEnabled) {
        auto error = audio.initialise(text(preferences, "audioState"));
        if (error.isNotEmpty())
            setStatus("Audio : " + error);
    }
    settings.quality.setSelectedId(std::clamp(preferences.value("whisperModel", 1), 1, 2),
                                   juce::dontSendNotification);
    if (settings.quality.getSelectedId() == 2 && !whisperModelAvailable("medium"))
        settings.quality.setSelectedId(1, juce::dontSendNotification);
    settings.language.setSelectedId(1, juce::dontSendNotification);
    settings.compute.setSelectedId(std::clamp(preferences.value("whisperCompute", 1), 1, 3),
                                   juce::dontSendNotification);
    settings.beats.setToggleState(preferences.value("analyseBeats", true), juce::dontSendNotification);
    translateControls(*this);
    translateControls(tabs);
    startTimerHz(25);
}
App::~App() {
    cancelFlag = true;
    queue.clear();
    if (worker.joinable())
        worker.join();
    waiting.end();
    dataWindow.reset();
    dialogs.clear();
    try {
        savePreferences();
    } catch (...) {
    }
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &theme)
        juce::LookAndFeel::setDefaultLookAndFeel(previousLook);
    setLookAndFeel(nullptr);
}
void App::savePreferences() {
    preferences["details"] = detailsOpen;
    preferences["extraVisual"] = extraVisual;
    preferences["timelineHeight"] = timelinePixels;
    preferences["instrumentHeight"] = preferredInstrumentHeight;
    preferences["instrumentsHidden"] = instrumentsHidden;
    preferences["timelineManual"] = timelineManual;
    preferences["tab"] = tabs.getCurrentTabIndex();
    preferences["audioVisible"] = timeline.audioVisible;
    preferences["chordsVisible"] = timeline.chordsVisible;
    preferences["lyricsVisible"] = timeline.lyricsVisible;
    preferences["settingsVisible"] = settingsView.isVisible();
    preferences["volume"] = volume.getValue();
    preferences["recordingGainDB"] = inputGain.getValue();
    preferences["whisperModel"] = settings.quality.getSelectedId();
    preferences["whisperCompute"] = settings.compute.getSelectedId();
    preferences["language"] = 1;
    preferences["analyseBeats"] = settings.beats.getToggleState();
    if (auto xml = audio.devices.createStateXml())
        preferences["audioState"] = utf(xml->toString());
    writeJson(dataFolder().getChildFile("settings.json"), preferences);
}
void App::mouseDown(const juce::MouseEvent &e) {
    if (e.position.y < 124)
        if (auto *w = parentWindow(this))
            windowDrag.startDraggingComponent(w, e);
}
void App::mouseDrag(const juce::MouseEvent &e) {
    if (e.getMouseDownY() < 124)
        if (auto *w = parentWindow(this))
            windowDrag.dragComponent(w, e, nullptr);
}
void App::mouseDoubleClick(const juce::MouseEvent &e) {
    if (e.position.y < 124)
        if (auto *w = parentWindow(this))
            w->setFullScreen(!w->isFullScreen());
}
void App::paint(juce::Graphics &g) {
    g.fillAll(theme.paper);
    if (logo.isValid())
        g.drawImageWithin(logo.getClippedImage({0, 0, logo.getWidth(), (int)(logo.getHeight() * .95)}), 18,
                          10, 130, 108, juce::RectanglePlacement::centred);
    g.setColour(theme.border);
    g.drawHorizontalLine(123, 18, (float)getWidth() - 18);
}
void App::resized() {
    int w = getWidth(), h = getHeight();
    title.setBounds(180, 8, std::max(50, w - 360), 27);
    int wx = w - 114;
    for (auto *c : {&windowMin, &windowMax, &windowClose}) {
        c->setBounds(wx, 6, 32, 32);
        wx += 32;
    }
    menu.setBounds(178, 58, 38, 38);
    newButton.setBounds(228, 58, 182, 38);
    analyseButton.setBounds(w - 334, 58, 172, 38);
    exportButton.setBounds(w - 152, 58, 134, 38);
    int x = 18;
    for (auto pair : std::vector<std::pair<juce::Component *, int>>{
             {&begin, 32}, {&play, 38}, {&stop, 32}, {&end, 32}, {&record, 34}, {&loop, 38}}) {
        pair.first->setBounds(x, 133, pair.second, 32);
        x += pair.second + (w - (settingsView.isVisible() ? 340 : 0) - 36 < 700 ? 4 : 7);
    }
    int right = settingsView.isVisible() ? 340 : 0;
    settingsView.setBounds(w - right + 4, 130, std::max(0, right - 12), h - 165);
    settings.setSize(std::max(280, settingsView.getMaximumVisibleWidth()), 1000);
    details.setBounds(12, 948, settings.getWidth() - 24, 36);
    int cw = w - right - 36;
    options.setBounds(18 + cw - 90, 133, 90, 30);
    clock.setBounds(x + 8, 133, cw < 800 ? 100 : 140, 32);
    // Keep playback volume and recording gain together, including compact windows.
    const bool compactLevels = options.getX() - clock.getRight() - 56 < 420;
    const int levelsX = compactLevels ? 18 : clock.getRight() + 36;
    const int levelsY = compactLevels ? 173 : 138;
    const int levelsWidth = compactLevels ? cw : options.getX() - levelsX - 20;
    const int gainWidth = std::min(290, (levelsWidth - 20) * 3 / 5);
    const int volumeWidth = std::min(265, levelsWidth - gainWidth - 20);
    const int volumeCaption = std::min(110, volumeWidth / 2);
    volumeLabel.setBounds(levelsX, levelsY, volumeCaption, 22);
    volume.setBounds(levelsX + volumeCaption, levelsY, volumeWidth - volumeCaption, 22);
    const int gainX = levelsX + volumeWidth + 20;
    const int gainCaption = std::min(100, gainWidth / 3);
    inputGainLabel.setBounds(gainX, levelsY - 1, gainCaption, 24);
    inputGain.setBounds(gainX + gainCaption, levelsY - 2, gainWidth - gainCaption, 26);
    const int compactOffset = compactLevels ? 32 : 0;
    device.setBounds(18, 171 + compactOffset, cw, 22);
    tracks.setBounds(18, 201 + compactOffset, 100, 30);
    filename.setBounds(132, 201 + compactOffset, std::max(50, cw - 490), 30);
    timeline.setSize(cw, timeline.getHeight());
    const int top = 242 + compactOffset, divider = 20;
    const int space = std::max(0, h - top - 42);
    const int timelineMin = std::min(space - divider, std::max(180, timeline.hintTextHeight() + 150));
    // Compact controls and previews, rather than the fullscreen 330–446 px floor.
    const int lowerMin = std::min(240, std::max(140, space - divider - timelineMin));
    const int lowerMax = std::max(lowerMin, space - divider - timelineMin);
    const int requested = timelineManual && preferredInstrumentHeight >= 0 ? preferredInstrumentHeight
                                                                           : space - divider - timelinePixels;
    const int lower = std::clamp(requested, lowerMin, lowerMax);
    const int th = instrumentsHidden ? std::max(0, space - divider) : std::max(0, space - divider - lower);
    timeline.setBounds(18, top, cw, th);
    split.setBounds(18, top + th, cw, divider);
    instruments.setVisible(!instrumentsHidden);
    instruments.setBounds(18, top + th + divider, cw, instrumentsHidden ? 0 : lower);
    split.fold.setToggleState(instrumentsHidden, juce::dontSendNotification);
    split.fold.setTooltip(instrumentsHidden ? tr("Afficher les instruments") : tr("Masquer les instruments"));
    const int footer = timeline.getBottom() - timeline.hintHeight();
    readableZoom.setBounds(18 + cw - 150, footer + (timeline.hintHeight() - 30) / 2, 150, 30);
    waveZoom.setBounds(readableZoom.getX() - 164, readableZoom.getY(), 150, 30);
    statusLabel.setBounds(18, h - 28, w - 88, 22);
    help.setBounds(w - 52, h - 38, 30, 30);
    waiting.setBounds((w - 350) / 2, (h - 350) / 2, 350, 350);
}
void App::toggleInstruments() {
    instrumentsHidden = !instrumentsHidden;
    resized();
    repaint();
}
void App::setStatus(String value) {
    status = tr(value);
    statusLabel.setText(status, juce::dontSendNotification);
}
void App::modified() {
    dirty = true;
    title.setText((project == File() ? tr("Sans titre") : project.getFileNameWithoutExtension()) + " *",
                  juce::dontSendNotification);
    if (auto *w = parentWindow(this))
        w->setName(juce::String::fromUTF8("KITHÁRA NÉA — ") + title.getText());
}
void App::snapshot() {
    history.push(doc);
    modified();
}
void App::refresh(bool preserveView) {
    filling = true;
    settings.refresh();
    static juce::StringArray families = {"guitar", "piano", "ukulele", "bass", "mandolin", "banjo"};
    int familyId = std::max(0, families.indexOf(text(doc.j, "instrument_family"))) + 1;
    instruments.family.setSelectedId(familyId == 2 ? 1 : familyId, juce::dontSendNotification);
    instruments.stringsAudition.setTooltip(
        text(doc.j, "instrument_family") == "bass"
            ? tr("Écouter le doigté affiché — basse : son de guitare provisoire")
            : tr("Écouter le doigté affiché avec cet instrument"));
    filling = false;
    chordTable.update();
    lyricTable.update();
    title.setText((project == File() ? tr("Sans titre") : project.getFileNameWithoutExtension()) +
                      (dirty ? " *" : tr("")),
                  juce::dontSendNotification);
    filename.setText(doc.audioName.isNotEmpty()       ? doc.audioName
                     : text(doc.j, "audio").isEmpty() ? tr("Aucun fichier ouvert")
                                                      : doc.audio().getFileName(),
                     juce::dontSendNotification);
    details.setButtonText(tr("Édition des données"));
    analyseButton.setEnabled(source != nullptr && !source->mono.empty());
    settings.transcribeButton.setEnabled(source != nullptr);
    exportButton.setEnabled(!doc.chords().empty() || text(doc.j, "song_sheet").isNotEmpty());
    if (!preserveView)
        timeline.fit();
    if (doc.chords().empty()) {
        selected = -1;
        shapes.clear();
        shapeIndex = 0;
        instruments.audition.setEnabled(false);
        instruments.stringsAudition.setEnabled(false);
    } else
        select(std::clamp(selected, 0, (int)doc.chords().size() - 1), false);
    resized();
    instruments.repaint();
    timeline.repaint();
}
void App::setLoaded(Document value, std::shared_ptr<AudioData> data, File file) {
    audio.stop();
    doc = std::move(value);
    source = std::move(data);
    timeline.prepareWaveform();
    audio.setSource(source);
    timeline.position = audio.seconds();
    project = file;
    history = {};
    dirty = false;
    selected = -1;
    refresh(false);
    timeline.restoreView(doc.j.value("timeline_view", J::object()));
    timeline.applyReadableZoom();
    if (project != File())
        rememberProject();
    setStatus(String((int)doc.chords().size()) + tr(" accords · ") + String((int)doc.lyrics().size()) +
              tr(" lignes de paroles"));
}
void App::select(int row, bool scroll) {
    if (row < 0 || row >= (int)doc.chords().size())
        return;
    selected = row;
    auto &c = doc.chords()[(size_t)row];
    try {
        shapes = theory.positions(label(c, doc), text(doc.j, "guitar_tuning"), doc.j.value("capo", 0));
        auto chosen = theory.chosen(c, doc);
        auto found = std::find(shapes.begin(), shapes.end(), chosen);
        shapeIndex = found == shapes.end() ? 0 : (int)std::distance(shapes.begin(), found);
    } catch (...) {
        shapes.clear();
        shapeIndex = 0;
    }
    instruments.audition.setEnabled(!pianoVoicing(c, doc, theory).notes().empty());
    instruments.stringsAudition.setEnabled(text(c, "name") != "N" && !shapes.empty());
    chordTable.select(row, scroll);
    instruments.repaint();
    timeline.repaint();
}
void App::seek(double seconds) {
    audio.seek(std::clamp(seconds, 0., doc.duration()));
    timeline.position = audio.seconds();
    if (timeline.position < timeline.offset || timeline.position > timeline.offset + timeline.span)
        timeline.follow(timeline.position);
    for (size_t i = 0; i < doc.chords().size(); ++i)
        if (doc.chords()[i].at("start").get<double>() <= timeline.position &&
            timeline.position < doc.chords()[i].at("end").get<double>()) {
            select((int)i, false);
            break;
        }
    timeline.repaint();
}
void App::moveBoundary(int row, double time) {
    if (jobActive || row < 0 || row + 1 >= (int)doc.chords().size())
        return;
    auto &a = doc.chords()[(size_t)row];
    auto &b = doc.chords()[(size_t)row + 1];
    time = std::clamp(time, a.at("start").get<double>() + .03, b.at("end").get<double>() - .03);
    snapshot();
    a["end"] = time;
    b["start"] = time;
    refresh();
}
void App::chooseShape() {
    if (jobActive || selected < 0 || shapes.empty())
        return;
    snapshot();
    shapeIndex = (shapeIndex + 1) % (int)shapes.size();
    doc.j["guitar_shapes"][utf(shapeKey(label(doc.chords()[(size_t)selected], doc),
                                        text(doc.j, "guitar_tuning"), doc.j.value("capo", 0)))] =
        shapes[(size_t)shapeIndex];
    timeline.repaint();
    instruments.repaint();
}
void App::auditionNotes(std::vector<int> notes, String timbre) {
    if (audio.isRecording() || notes.empty())
        return;
    try {
        if (!synth)
            synth = std::make_unique<Synth>();
        audio.preview(synth->chord(notes, 1.2, timbre), synth->rate);
    } catch (const std::exception &e) {
        message(*this, tr("Écouter l’accord"), String::fromUTF8(e.what()));
    }
}
void App::audition() {
    if (selected < 0 || selected >= (int)doc.chords().size()) {
        setStatus(tr("Choisir un accord à écouter."));
        return;
    }
    auto &c = doc.chords()[(size_t)selected];
    auditionNotes(pianoVoicing(c, doc, theory).notes(), "upright");
}
void App::auditionStrings() {
    if (selected < 0 || selected >= (int)doc.chords().size() || shapes.empty() || shapeIndex < 0 ||
        shapeIndex >= (int)shapes.size() || text(doc.chords()[(size_t)selected], "name") == "N")
        return;
    auditionNotes(stringVoicing(doc, shapes[(size_t)shapeIndex]), stringTimbre(doc));
}
void App::changeArrangement() {
    if (filling || jobActive)
        return;
    const auto previousDocument = doc;
    snapshot();
    int delta = (int)settings.transpose.getValue() - doc.j.value("transpose", 0);
    if (delta && text(doc.j, "song_sheet").isNotEmpty()) {
        auto old = utf(text(doc.j, "song_sheet"));
        // Escape the closing bracket inside the character class: MSVC rejects
        // the unescaped form and throws before any chord can be transposed.
        static const std::regex pattern(R"(\[([A-G](?:#|b)?[^\]\r\n]*)\])");
        std::string changed;
        size_t previous = 0;
        for (std::sregex_iterator i(old.begin(), old.end(), pattern), end; i != end; ++i) {
            auto m = *i;
            changed += old.substr(previous, (size_t)m.position() - previous) + "[" +
                       utf(transpose(str(m[1]), delta)) + "]";
            previous = m.position() + m.length();
        }
        changed += old.substr(previous);
        doc.j["song_sheet"] = changed;
    }
    doc.j["transpose"] = (int)settings.transpose.getValue();
    doc.j["capo"] = (int)settings.capo.getValue();
    updateAutomaticSheet(doc, previousDocument);
    refresh();
}
void App::changeFamily() {
    if (jobActive)
        return;
    static std::vector<std::string> names = {"guitar", "piano", "ukulele", "bass", "mandolin", "banjo"};
    int index = instruments.family.getSelectedId() - 1;
    if (index < 0 || index >= 6 || index == 1)
        return;
    snapshot();
    doc.j["instrument_family"] = names[(size_t)index];
    for (auto &row : theory.tunings)
        if (row.at("instrument") == names[(size_t)index]) {
            doc.j["guitar_tuning"] = row.at("tuning");
            break;
        }
    refresh();
}
void App::toggleDetails() {
    if (!dataWindow)
        dataWindow = std::make_unique<DataWindow>(*this);
    dataWindow->reopen();
    dataWindow->toFront(true);
}
void App::undo() {
    if (jobActive)
        return;
    doc = history.undo(doc);
    dirty = true;
    refresh();
}
void App::redo() {
    if (jobActive)
        return;
    doc = history.redo(doc);
    dirty = true;
    refresh();
}
void App::timerCallback() {
    if (recordingDeadline > 0) {
        if (juce::Time::getMillisecondCounterHiRes() >= recordingDeadline)
            beginRecordingNow();
        else
            timeline.repaint();
    }
    double seconds;
    bool playing, recording;
    {
        juce::ScopedLock guard(audio.lock);
        seconds = audio.position / 16000. * audio.timeFactor;
        recording = audio.writer != nullptr;
        if (recording)
            seconds = audio.recordingSeconds;
        playing = audio.playing;
    }
    clock.setText(recording
                      ? String::formatted("%d:%05.2f", (int)seconds / 60, std::fmod(seconds, 60.))
                      : String::formatted("%d:%05.2f / %d:%02d", (int)seconds / 60, std::fmod(seconds, 60.),
                                          (int)doc.duration() / 60, (int)doc.duration() % 60),
                  juce::dontSendNotification);
    play.setToggleState(playing, juce::dontSendNotification);
    play.setButtonText(playing ? tr("Pause") : tr("Lecture"));
    record.setToggleState(recording, juce::dontSendNotification);
    if (recording) {
        timeline.repaint();
        juce::ScopedLock lock(audio.lock);
        if (audio.recordingWriteFailed)
            setStatus(tr("Erreur d’écriture de la prise audio."));
        else if (audio.recordingClipped)
            setStatus(tr("Saturation pendant la prise : réduire le gain d’entrée."));
        else if (audio.recordingSeconds > 2 && audio.recordedPeak < .00001f)
            setStatus(tr("Aucun signal audible : vérifier l’entrée audio et le microphone."));
    }
    if (auto *devicePtr = audio.devices.getCurrentAudioDevice())
        device.setText(devicePtr->getName() + juce::String::fromUTF8(" · ") +
                           String((int)devicePtr->getCurrentSampleRate()) + " Hz",
                       juce::dontSendNotification);
    if (playing) {
        int currentLyric = -1;
        for (size_t i = 0; i < doc.lyrics().size(); ++i)
            if (doc.lyrics()[i].at("start").get<double>() <= seconds &&
                seconds < doc.lyrics()[i].at("end").get<double>()) {
                currentLyric = (int)i;
                break;
            }
        if (lyricTable.table.getSelectedRow() != currentLyric) {
            if (currentLyric < 0)
                lyricTable.table.deselectAllRows();
            else
                lyricTable.select(currentLyric, true);
        }
        // The display callback moves the waveform; this timer updates controls only.
        for (size_t i = 0; i < doc.chords().size(); ++i)
            if (doc.chords()[i].at("start").get<double>() <= seconds &&
                seconds < doc.chords()[i].at("end").get<double>()) {
                if ((int)i != selected)
                    select((int)i);
                break;
            }
    }
}
bool App::keyPressed(const juce::KeyPress &key) {
    if (recordingDeadline > 0 && (key == juce::KeyPress::escapeKey || key == juce::KeyPress::spaceKey)) {
        cancelRecordingCountdown();
        return true;
    }
    if (audio.isRecording() && key == juce::KeyPress::spaceKey) {
        recordAudio();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && jobActive) {
        cancel();
        return true;
    }
    auto *focus = juce::Component::getCurrentlyFocusedComponent();
    bool typing = dynamic_cast<juce::TextEditor *>(focus) != nullptr;
    if (typing)
        return false;
    bool control = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    if (control) {
        auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        if (c == 's') {
            save(key.getModifiers().isShiftDown());
            return true;
        }
        if (c == 'o') {
            if (key.getModifiers().isShiftDown())
                openProject();
            else
                openAudio();
            return true;
        }
        if (c == 'z') {
            undo();
            return true;
        }
        if (c == 'y') {
            redo();
            return true;
        }
        if (c == 'k') {
            splitChord();
            return true;
        }
        if (c == 'm') {
            mergeChord();
            return true;
        }
        if (c == 'b') {
            marker();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::leftKey) {
            seek(audio.seconds() - 5);
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::rightKey) {
            seek(audio.seconds() + 5);
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::homeKey) {
            seek(0);
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::endKey) {
            seek(doc.duration());
            return true;
        }
    }
    if (key == juce::KeyPress::spaceKey) {
        play.triggerClick();
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        editChord();
        return true;
    }
    if (key == juce::KeyPress::deleteKey) {
        clearChord();
        return true;
    }
    if (key == juce::KeyPress::F1Key) {
        shortcuts();
        return true;
    }
    if (key == juce::KeyPress::homeKey) {
        timeline.applyReadableZoom();
        return true;
    }
    if (juce::CharacterFunctions::toLowerCase(key.getTextCharacter()) == 'r') {
        recordAudio();
        return true;
    }
    return false;
}
void App::runJob(String name, std::function<JobResult(Cancel)> work,
                 std::function<void(JobResult)> complete) {
    if (jobActive) {
        if (std::none_of(queue.begin(), queue.end(), [&](auto &item) { return item.caption == name; }))
            queue.push_back({name, std::move(work), std::move(complete)});
        return;
    }
    if (worker.joinable())
        worker.join();
    jobActive = true;
    jobProgress = -1;
    cancelFlag = false;
    newButton.setEnabled(false);
    record.setEnabled(false);
    waiting.begin(name);
    auto safe = safePointer();
    worker = std::thread([this, safe, work = std::move(work), complete = std::move(complete)]() mutable {
        auto result = std::make_shared<JobResult>();
        String error;
        try {
            *result = work([this] { return cancelFlag.load(); });
            checkCancel([this] { return cancelFlag.load(); });
        } catch (const std::exception &e) {
            error = String::fromUTF8(e.what());
        }
        juce::MessageManager::callAsync([safe, result, error, complete = std::move(complete)]() mutable {
            if (safe)
                safe->finished(result, error, std::move(complete));
        });
    });
}
void App::finished(std::shared_ptr<JobResult> result, String error, std::function<void(JobResult)> complete) {
    if (worker.joinable())
        worker.join();
    jobActive = false;
    waiting.end();
    newButton.setEnabled(true);
    record.setEnabled(true);
    if (error.isEmpty() && !cancelFlag) {
        try {
            complete(std::move(*result));
        } catch (const std::exception &e) {
            error = String::fromUTF8(e.what());
        }
    }
    if (error.isNotEmpty()) {
        setStatus(error);
        queue.clear();
        if (!cancelFlag && !closing)
            message(*this, tr("Traitement"), error);
    }
    if (closing) {
        closing = false;
        requestClose(closeCallback);
        return;
    }
    if (!queue.empty() && !cancelFlag) {
        auto next = std::move(queue.front());
        queue.pop_front();
        runJob(next.caption, std::move(next.work), std::move(next.complete));
    }
}
void App::cancel() {
    cancelFlag = true;
    queue.clear();
    waiting.repaint();
}
void App::requestClose(std::function<void()> callback) {
    cancelRecordingCountdown();
    if (jobActive) {
        closing = true;
        closeCallback = std::move(callback);
        cancel();
        return;
    }
    if (audio.isRecording()) {
        if (!juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, tr("Enregistrement"),
                                                tr("Terminer et conserver la prise avant de quitter ?"),
                                                "Conserver", tr("Annuler"), this))
            return;
        auto f = audio.endRecording();
        setStatus(tr("Prise conservée : ") + f.getFullPathName());
    }
    if (confirmChanges())
        callback();
}
bool App::confirmChanges() {
    if (!dirty)
        return true;
    int answer = juce::AlertWindow::showYesNoCancelBox(
        juce::MessageBoxIconType::QuestionIcon, tr("Projet modifié"),
        tr("Enregistrer les changements du projet ?"), tr("Enregistrer"), tr("Ignorer"), tr("Annuler"), this);
    return answer == 2 || (answer == 1 && save());
}
void App::newProject() {
    if (jobActive || recordingDeadline > 0 || audio.isRecording() || !confirmChanges())
        return;
    setLoaded(Document(), nullptr);
    loop.setToggleState(false, juce::dontSendNotification);
    setStatus(tr("Ouvrir un fichier audio ou enregistrer au microphone."));
}
void App::openAudio(File file, bool recorded) {
    if (jobActive || recordingDeadline > 0 || audio.isRecording() || (!recorded && !confirmChanges()))
        return;
    if (!file.existsAsFile()) {
        juce::FileChooser chooser(tr("Ouvrir un fichier audio"), {},
                                  "*.wav;*.mp3;*.flac;*.ogg;*.m4a;*.aif;*.aiff;*.mp4");
        if (!chooser.browseForFileToOpen())
            return;
        file = chooser.getResult();
    }
    audio.pause();
    runJob(
        tr("Ouverture de l’audio"),
        [file, recorded](Cancel cancel) {
            JobResult r;
            r.audio = loadAudio(file, cancel);
            Document d;
            d.j["audio"] = utf(file.getFullPathName());
            d.j["duration"] = r.audio->seconds();
            d.audioName = file.getFileName();
            if (recorded || (file.getParentDirectory() == dataFolder() && file.getFileName().startsWith("Prise-")))
                d.j["audio_origin"] = "recording";
            d.mediaMode = d.ownsAudio() ? "embedded" : "external";
            r.document = d;
            return r;
        },
        [this](JobResult r) {
            setLoaded(*r.document, r.audio);
            if (doc.ownsAudio())
                dirty = true;
            setStatus(tr("Audio ouvert · aucune analyse effectuée"));
        });
}
void App::openProject(File file) {
    if (jobActive || recordingDeadline > 0 || audio.isRecording() || !confirmChanges())
        return;
    if (!file.existsAsFile()) {
        juce::FileChooser chooser(tr("Ouvrir un projet"), {}, "*.knea;*.json");
        if (!chooser.browseForFileToOpen())
            return;
        file = chooser.getResult();
    }
    audio.pause();
    runJob(
        tr("Ouverture du projet"),
        [file](Cancel cancel) {
            JobResult r;
            r.document = Document::load(file);
            checkCancel(cancel);
            if (r.document->audio().existsAsFile())
                r.audio = loadAudio(r.document->audio(), cancel);
            return r;
        },
        [this, file](JobResult r) {
            bool missing = text(r.document->j, "audio").isNotEmpty() && !r.document->audio().existsAsFile();
            setLoaded(*r.document, r.audio, file);
            if (missing) {
                setStatus(tr("Projet ouvert sans audio — média introuvable"));
                if (juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
                                                       tr("Audio manquant"),
                                                       tr("Retrouver le fichier audio de ce projet ?"),
                                                       "Retrouver", tr("Plus tard"), this)) {
                    juce::FileChooser chooser(tr("Retrouver l’audio"), file.getParentDirectory(), "*.*");
                    if (chooser.browseForFileToOpen()) {
                        auto path = chooser.getResult();
                        auto current = doc;
                        runJob(
                            tr("Ouverture du média"),
                            [current, path](Cancel cancel) mutable {
                                JobResult result;
                                current.j["audio"] = utf(path.getFullPathName());
                                current.media.reset();
                                current.mediaMode = "external";
                                result.document = current;
                                result.audio = loadAudio(path, cancel);
                                return result;
                            },
                            [this, file](JobResult result) {
                                setLoaded(*result.document, result.audio, file);
                                modified();
                            });
                    }
                }
            }
        });
}
bool App::save(bool saveAs) {
    if (jobActive || recordingDeadline > 0 || audio.isRecording())
        return false;
    auto path = project;
    bool embed = doc.mediaMode != "external" || doc.ownsAudio();
    if (path == File() || saveAs || !path.hasFileExtension("knea")) {
        juce::FileChooser chooser(
            tr("Enregistrer un projet autonome .knea"),
            path == File() ? File::getSpecialLocation(File::userDocumentsDirectory)
                                 .getChildFile((doc.audioName.isNotEmpty()
                                                    ? File(doc.audioName).getFileNameWithoutExtension()
                                                    : "Projet") +
                                               ".knea")
                           : path.withFileExtension("knea"),
            "*.knea");
        if (!chooser.browseForFileToSave(true))
            return false;
        path = chooser.getResult().withFileExtension("knea");
        embed = true;
    }
    try {
        auto saved = doc;
        saved.j["timeline_view"] = timeline.viewState();
        saved.save(path, embed);
        project = path;
        doc.mediaMode = embed ? "embedded" : "external";
        dirty = false;
        refresh();
        rememberProject();
        setStatus(tr("Projet enregistré"));
        return true;
    } catch (const std::exception &e) {
        message(*this, tr("Enregistrer le projet"), String::fromUTF8(e.what()));
        return false;
    }
}
void App::saveLight() {
    if (jobActive || recordingDeadline > 0 || audio.isRecording())
        return;
    auto value = doc;
    const bool embedRecording = value.ownsAudio();
    if (value.media && !embedRecording) {
        juce::FileChooser chooser(tr("Choisir l’audio original du projet léger"), {}, "*.*");
        if (!chooser.browseForFileToOpen())
            return;
        auto original = chooser.getResult();
        if (sha256(original) != sha256(value.audio())) {
            message(*this, "Audio", tr("Le fichier choisi diffère de l’audio intégré."));
            return;
        }
        value.j["audio"] = utf(original.getFullPathName());
        value.media.reset();
    }
    auto folder = project == File() ? File::getSpecialLocation(File::userDocumentsDirectory)
                                    : project.getParentDirectory();
    auto name =
        project != File()
            ? project.getFileNameWithoutExtension()
            : (value.audioName.isNotEmpty() ? File(value.audioName).getFileNameWithoutExtension() : "Projet");
    if (!name.endsWithIgnoreCase("_linked"))
        name += "_linked";
    File path;
    {
        juce::FileChooser chooser(tr("Enregistrer un projet léger"), folder.getChildFile(name + ".knea"),
                                  "*.knea");
        if (!chooser.browseForFileToSave(true))
            return;
        path = chooser.getResult().withFileExtension("knea");
    }
    try {
        value.j["timeline_view"] = timeline.viewState();
        value.save(path, embedRecording);
        doc = value;
        doc.mediaMode = embedRecording ? "embedded" : "external";
        project = path;
        dirty = false;
        rememberProject();
        refresh();
        setStatus(tr("Projet léger enregistré"));
    } catch (const std::exception &e) {
        message(*this, tr("Enregistrer"), String::fromUTF8(e.what()));
    }
}
void App::rememberProject() {
    J recent = J::array();
    recent.push_back(utf(project.getFullPathName()));
    for (auto &p : preferences.value("recentProjects", J::array()))
        if (p != recent[0] && recent.size() < 20)
            recent.push_back(p);
    preferences["recentProjects"] = recent;
}
void App::analyse() {
    if (!source || recordingDeadline > 0 || audio.isRecording())
        return;
    if (doc.chords().empty()) {
        startChordAnalysis();
        return;
    }
    struct Question : juce::Component {
        juce::Label text;
        juce::TextButton yes{tr("Oui")}, no{tr("Non")};
        Question() {
            addAndMakeVisible(text);
            addAndMakeVisible(yes);
            addAndMakeVisible(no);
            text.setText(tr("Une analyse d’accords existe déjà. Voulez-vous la remplacer ?"),
                         juce::dontSendNotification);
            text.setFont(juce::FontOptions("Segoe UI", 18, juce::Font::plain));
        }
        void resized() override {
            text.setBounds(20, 12, getWidth() - 40, 76);
            yes.setBounds(getWidth() - 230, getHeight() - 50, 100, 34);
            no.setBounds(getWidth() - 120, getHeight() - 50, 100, 34);
        }
    };
    auto content = std::make_unique<Question>();
    auto *controls = content.get();
    auto *dialog = presentDialog(tr("Analyse existante"), std::move(content), 540, 200,
                                 [safe = safePointer()](int answer, juce::Component &) {
                                     if (safe && answer == 1)
                                         safe->startChordAnalysis();
                                 });
    juce::Component::SafePointer<CloseableDialog> safe(dialog);
    controls->yes.onClick = [safe] {
        if (safe)
            safe->exitModalState(1);
    };
    controls->no.onClick = [safe] {
        if (safe)
            safe->exitModalState(0);
    };
}
void App::startChordAnalysis() {
    if (!source || recordingDeadline > 0 || audio.isRecording())
        return;
    auto current = doc;
    auto data = source;
    bool beats = settings.beats.getToggleState();
    audio.pause();
    runJob(
        tr("Analyse en cours"),
        [this, current, data, beats](Cancel cancel) {
            JobResult r;
            r.document = analyseHarmony(current, theory, cancel, {});
            if (beats)
                analyseBeats(*r.document, *data, cancel, {});
            r.audio = data;
            return r;
        },
        [this](JobResult r) { finishChordAnalysis(std::move(r)); });
}
void App::finishChordAnalysis(JobResult r) {
    const auto previous = doc;
    snapshot();
    doc = *r.document;
    // Chord analysis owns harmony/rhythm, never the lyric alignment or user sheet.
    for (const char *key : {"lyrics", "lyrics_language", "reference_text", "reference_matches", "song_sheet",
                            "song_sheet_generated"}) {
        if (previous.j.contains(key))
            doc.j[key] = previous.j[key];
        else
            doc.j.erase(key);
    }
    updateAutomaticSheet(doc, previous);
    refresh();
    timeline.applyReadableZoom();
    setStatus(String((int)doc.chords().size()) + tr(" accords · analyse terminée"));
}
void App::transcribeLyrics() {
    if (settings.quality.getSelectedId() == 2 && !whisperModelAvailable("medium")) {
        selectWhisperQuality();
        return;
    }
    if (!source || recordingDeadline > 0 || audio.isRecording())
        return;
    if ((!doc.lyrics().empty() || text(doc.j, "reference_text").isNotEmpty()) &&
        !juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon, tr("Paroles existantes"),
            tr("Remplacer la transcription et son alignement ? Le texte de référence sera retiré de cette "
               "version ; Annuler permettra de le restaurer."),
            "Transcrire", tr("Annuler"), this))
        return;
    String language = "auto";
    auto model = settings.quality.getSelectedId() == 2 ? String("medium") : String("small");
    const String compute = settings.compute.getSelectedId() == 2 ? "cpu" :
                           settings.compute.getSelectedId() == 3 ? "gpu" : "auto";
    auto data = source;
    audio.pause();
    runJob(
        tr("Transcription en cours"),
        [data, model, language, compute](Cancel cancel) {
            JobResult r;
            r.payload = transcribe(*data, model, language, cancel, {}, compute);
            return r;
        },
        [this](JobResult r) {
            if (r.payload.at("lyrics").empty()) {
                setStatus(tr("Aucune parole détectée ; les paroles précédentes sont conservées."));
                return;
            }
            const auto previous = doc;
            snapshot();
            doc.lyrics() = r.payload.at("lyrics");
            doc.j["lyrics_language"] = r.payload.at("language");
            doc.j["reference_text"] = tr("");
            doc.j["reference_matches"] = J::array();
            updateAutomaticSheet(doc, previous);
            timeline.lyricsVisible = true;
            refresh();
            tabs.setCurrentTabIndex(1);
            setStatus(tr("Transcription terminée") + (r.payload.value("compute_fallback", false)
                ? tr(" · GPU indisponible, calcul effectué sur CPU") : String()));
        });
}
void App::alignLyrics() {
    String value = text(doc.j, "reference_text");
    if (value.isEmpty())
        for (auto &cue : doc.lyrics())
            value += (value.isEmpty() ? String() : "\n") + text(cue, "text");
    textDialog(
        *this, tr("Saisir et aligner les paroles"),
        tr("Conservez vos sauts de ligne et lignes vides. Les mots non retrouvés restent sans horodatage."),
        value, source != nullptr, false, [safe = safePointer()](int action, String value) {
            if (safe)
                safe->acceptReference(value, action);
        });
}
void App::acceptReference(String value, int action) {
    if (!action)
        return;
    snapshot();
    if (text(doc.j, "reference_text") != value)
        doc.j["reference_matches"] = J::array();
    doc.j["reference_text"] = utf(value);
    if (action == 1 || !source) {
        refresh();
        setStatus(tr("Texte conservé · alignement à lancer à la demande"));
        return;
    }
    if (settings.quality.getSelectedId() == 2 && !whisperModelAvailable("medium")) {
        selectWhisperQuality();
        return;
    }
    auto data = source;
    String language = "auto";
    auto model = settings.quality.getSelectedId() == 2 ? String("medium") : String("small");
    const String compute = settings.compute.getSelectedId() == 2 ? "cpu" :
                           settings.compute.getSelectedId() == 3 ? "gpu" : "auto";
    runJob(
        tr("Alignement en cours"),
        [data, value, language, model, compute](Cancel cancel) {
            auto recognized = transcribe(*data, model, language, cancel, {}, compute);
            auto result = align(value, recognized.at("lyrics"), cancel);
            JobResult r;
            r.payload = {{"matches", result.first},
                         {"lyrics", result.second},
                         {"language", recognized.at("language")},
                         {"compute_fallback", recognized.value("compute_fallback", false)},
                         {"recognized_segments", recognized.at("lyrics").size()}};
            return r;
        },
        [this](JobResult r) { finishAlignment(std::move(r)); });
}
void App::finishAlignment(JobResult r) {
    if (r.payload.at("matches").empty() || r.payload.at("lyrics").empty()) {
        if (r.payload.value("recognized_segments", size_t(0)) == 0)
            setStatus(tr("Aucune parole détectée, alignement impossible. Le texte saisi est conservé."));
        else
            setStatus(tr("Aucun repère alignable trouvé. Le texte saisi et les horodatages existants sont "
                         "conservés."));
        refresh();
        return;
    }
    const auto previous = doc;
    snapshot();
    doc.j["reference_matches"] = r.payload.at("matches");
    doc.lyrics() = r.payload.at("lyrics");
    doc.j["lyrics_language"] = r.payload.at("language");
    updateAutomaticSheet(doc, previous);
    timeline.lyricsVisible = true;
    refresh();
    tabs.setCurrentTabIndex(1);
    setStatus(String((int)doc.j.at("reference_matches").size()) + tr(" mots alignés · vérifier à l’écoute") +
              (r.payload.value("compute_fallback", false)
                   ? tr(" · GPU indisponible, calcul effectué sur CPU") : String()));
}
void App::alignmentReport() {
    String report = text(doc.j, "reference_text") + tr("\n\nRepères retrouvés :\n");
    for (auto &w : doc.j.at("reference_matches"))
        report += String(w.at("start").get<double>(), 2) + juce::String::fromUTF8(" – ") +
                  String(w.at("end").get<double>(), 2) + juce::String::fromUTF8(" s · ") + text(w, "text") +
                  juce::String::fromUTF8(" · ") + text(w, "kind") + "\n";
    textDialog(*this, tr("Vérifier l’alignement"),
               tr("Les mots non retrouvés ne reçoivent pas de temps inventé. Vérifier les rapprochements à "
                  "l’écoute."),
               report, false, true);
}
void App::cancelRecordingCountdown() {
    if (recordingDeadline <= 0)
        return;
    recordingDeadline = 0;
    record.setToggleState(false, juce::dontSendNotification);
    timeline.repaint();
    setStatus(tr("Décompte annulé · aucune prise créée"));
}
void App::beginRecordingNow() {
    if (jobActive) {
        cancelRecordingCountdown();
        return;
    }
    recordingDeadline = 0;
    try {
        const auto file = dataFolder().getChildFile(
            "Prise-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav");
        require(!file.existsAsFile(), tr("Une prise de ce nom existe déjà. Réessayer."));
        audio.record(file);
        record.setToggleState(true, juce::dontSendNotification);
        setStatus(tr("Enregistrement · R ou Espace pour terminer"));
    } catch (const std::exception &e) {
        message(*this, tr("Enregistrement"), String::fromUTF8(e.what()));
    }
    timeline.repaint();
}
void App::recordAudio() {
    if (jobActive)
        return;
    if (recordingDeadline > 0) {
        cancelRecordingCountdown();
        return;
    }
    try {
        if (!audio.isRecording()) {
            if (!confirmChanges())
                return;
            audio.pause();
            recordingDeadline = juce::Time::getMillisecondCounterHiRes() + 3000;
            record.setToggleState(true, juce::dontSendNotification);
            setStatus(tr("Décompte · Espace ou Échap pour annuler"));
            timeline.repaint();
            grabKeyboardFocus();
        } else {
            auto file = audio.endRecording();
            bool failed;
            float peak;
            {
                juce::ScopedLock lock(audio.lock);
                failed = audio.recordingWriteFailed;
                peak = audio.recordedPeak;
            }
            timeline.repaint();
            if (failed) {
                message(*this, tr("Enregistrement"), tr("Erreur d’écriture de la prise audio."));
                return;
            }
            if (peak < .00001f) {
                message(*this, tr("Enregistrement"),
                        tr("Aucun signal audible : vérifier l’entrée audio et le microphone."));
                return;
            }
            openAudio(file, true);
        }
    } catch (const std::exception &e) {
        message(*this, tr("Enregistrement"), String::fromUTF8(e.what()));
    }
}
void App::applyPlayback() {
    if (!source || jobActive)
        return;
    double speed = settings.speed.getValue();
    int semitones = doc.j.value("transpose", 0);
    auto input = doc.audio();
    auto current = doc;
    runJob(
        tr("Préparation de l’écoute"),
        [input, current, speed, semitones](Cancel cancel) {
            auto temp = std::make_shared<TempFolder>();
            auto out = temp->folder.getChildFile("playback.wav");
            double pitch = std::pow(2., semitones / 12.), tempo = speed / pitch;
            String filters = "asetrate=" + String(16000 * pitch, 8) + ",aresample=16000";
            while (tempo < .5) {
                filters += ",atempo=0.5";
                tempo /= .5;
            }
            while (tempo > 2) {
                filters += ",atempo=2";
                tempo /= 2;
            }
            filters += ",atempo=" + String(tempo, 8);
            auto original = loadAudio(input, cancel);
            auto wav = temp->folder.getChildFile("source.wav");
            saveWave(wav, original->mono, 16000);
            runProcess({resource("bin/ffmpeg.exe").getFullPathName(), "-v", "error", "-nostdin",
                        "-protocol_whitelist", "file,pipe", "-i", wav.getFullPathName(), "-af", filters, "-y",
                        out.getFullPathName()},
                       cancel);
            JobResult r;
            r.audio = loadAudio(out, cancel);
            r.payload = {{"factor", speed}};
            return r;
        },
        [this, speed](JobResult r) {
            auto position = audio.seconds();
            audio.setSource(r.audio);
            {
                juce::ScopedLock guard(audio.lock);
                audio.speed = 1;
                audio.timeFactor = speed;
            }
            audio.seek(position);
            setStatus(tr("Réglages appliqués à l’écoute"));
        });
}
void App::menuPopup() {
    juce::PopupMenu m;
    m.setLookAndFeel(&theme);
    m.addItem(19, tr("Nouveau projet"));
    m.addSeparator();
    m.addItem(1, tr("Ouvrir un audio…    Ctrl+O"));
    m.addItem(2, tr("Ouvrir un projet…    Ctrl+Maj+O"));
    m.addItem(3, tr("Enregistrer    Ctrl+S"));
    m.addItem(4, tr("Enregistrer sous…"));
    m.addItem(5, tr("Enregistrer un projet léger…"));
    juce::PopupMenu recent;
    recent.setLookAndFeel(&theme);
    int recentId = 100;
    for (auto &file : preferences.value("recentProjects", J::array())) {
        File f(str(file.get<std::string>()));
        if (f.existsAsFile())
            recent.addItem(recentId, f.getFileNameWithoutExtension());
        ++recentId;
    }
    m.addSubMenu(tr("Projets récents"), recent);
    m.addSeparator();
    m.addItem(10, tr("Préparer la song sheet…"));
    m.addItem(11, tr("Exporter…"));
    m.addSeparator();
    m.addItem(12, tr("Annuler    Ctrl+Z"), !history.back.empty());
    m.addItem(13, tr("Rétablir    Ctrl+Y"), !history.forward.empty());
    m.addItem(14, tr("Afficher les réglages"), true, settingsView.isVisible());
    m.addItem(15, tr("Entrées et sorties audio…"));
    m.addItem(16, tr("Crédits et licences tierces…"));
    m.addItem(17, tr("À propos"));
    m.addSeparator();
    m.addItem(18, tr("Quitter"));
    juce::PopupMenu languages;
    languages.setLookAndFeel(&theme);
    languages.addItem(30, tr("Français"), true, !isEnglish());
    languages.addItem(31, "English", true, isEnglish());
    m.addSubMenu(tr("Langue"), languages);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menu), [safe = safePointer()](int id) {
        if (!safe)
            return;
        auto &a = *safe;
        if (id == 19)
            a.newProject();
        if (id == 30 || id == 31)
            a.setLanguage(id == 31);
        if (id == 1)
            a.openAudio();
        if (id == 2)
            a.openProject();
        if (id == 3)
            a.save();
        if (id == 4)
            a.save(true);
        if (id == 5)
            a.saveLight();
        if (id >= 100) {
            auto list = a.preferences.value("recentProjects", J::array());
            if ((size_t)(id - 100) < list.size())
                a.openProject(File(str(list[(size_t)id - 100].get<std::string>())));
        }
        if (id == 10)
            a.editSheet();
        if (id == 11)
            a.exportPopup(&a.menu);
        if (id == 12)
            a.undo();
        if (id == 13)
            a.redo();
        if (id == 14) {
            a.settingsView.setVisible(!a.settingsView.isVisible());
            a.resized();
        }
        if (id == 15)
            a.audioSettings();
        if (id == 16)
            a.licenses();
        if (id == 17)
            a.about();
        if (id == 18)
            if (auto *w = parentWindow(&a))
                w->closeButtonPressed();
    });
}
void App::exportPopup(juce::Component *origin) {
    juce::PopupMenu m;
    m.setLookAndFeel(&theme);
    juce::StringArray items = {tr("Song sheet PDF…"),       tr("Song sheet ChordPro…"),
                               tr("Grille d’accords PDF…"), tr("Accompagnement MIDI…"),
                               tr("Accompagnement WAV…"),   tr("Accords CSV…"),
                               tr("Accords texte…")};
    for (int i = 0; i < items.size(); ++i)
        m.addItem(i + 1, items[i]);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(origin ? origin : &exportButton),
                    [safe = safePointer()](int id) {
                        if (!safe || id < 1)
                            return;
                        juce::StringArray types = {"song", "cho", "pdf", "mid", "wav", "csv", "txt"};
                        safe->exportFile(types[id - 1]);
                    });
}
void App::exportFile(String kind) {
    if (jobActive)
        return;
    if ((kind == "song" || kind == "cho") && text(doc.j, "song_sheet").isEmpty()) {
        editSheet();
        if (text(doc.j, "song_sheet").isEmpty())
            return;
    }
    auto extension = kind == "song" ? "pdf" : kind;
    auto base = project == File() ? File::getSpecialLocation(File::userDocumentsDirectory)
                                        .getChildFile(doc.audioName.isNotEmpty()
                                                          ? File(doc.audioName).getFileNameWithoutExtension()
                                                          : "Morceau")
                                  : project;
    const auto suggested =
        kind == "pdf" ? base.getSiblingFile(
                            (project == File() ? base.getFileName() : base.getFileNameWithoutExtension()) +
                            "_chart.pdf")
                      : base.withFileExtension(kind == "song" ? "song.pdf" : extension);
    juce::FileChooser chooser(tr("Exporter"), suggested, "*." + extension);
    if (!chooser.browseForFileToSave(true))
        return;
    auto file = chooser.getResult();
    try {
        if (kind == "song" || kind == "pdf")
            exportPdf(doc, theory, file, kind == "song");
        else if (kind == "cho")
            exportChordPro(doc, file);
        else if (kind == "mid")
            exportMidi(doc, file);
        else if (kind == "csv")
            exportCsv(doc, file);
        else if (kind == "txt")
            exportText(doc, file);
        else if (kind == "wav") {
            auto copy = doc;
            runJob(
                tr("Export audio en cours"),
                [copy, file](Cancel cancel) {
                    checkCancel(cancel);
                    Synth renderer;
                    exportWave(copy, renderer, file, cancel);
                    checkCancel(cancel);
                    return JobResult{};
                },
                [this, file](JobResult) { setStatus(tr("Exporté : ") + file.getFullPathName()); });
            return;
        }
        setStatus(tr("Exporté : ") + file.getFullPathName());
    } catch (const std::exception &e) {
        message(*this, tr("Exporter"), String::fromUTF8(e.what()));
    }
}
void App::chordPopup(int row) {
    select(row);
    juce::PopupMenu m;
    m.setLookAndFeel(&theme);
    m.addItem(1, tr("Modifier l’accord…"));
    m.addItem(2, tr("Scinder à la tête de lecture"));
    m.addItem(3, tr("Fusionner avec le suivant"));
    m.addItem(4, tr("Marquer sans accord"));
    m.addItem(5, tr("Écouter l’accord"));
    m.showMenuAsync({}, [safe = safePointer()](int id) {
        if (!safe)
            return;
        if (id == 1)
            safe->editChord();
        if (id == 2)
            safe->splitChord();
        if (id == 3)
            safe->mergeChord();
        if (id == 4)
            safe->clearChord();
        if (id == 5)
            safe->audition();
    });
}
void App::editChord() {
    if (jobActive || selected < 0 || selected >= (int)doc.chords().size())
        return;
    auto &c = doc.chords()[(size_t)selected];
    juce::AlertWindow dialog(tr("Modifier l’accord"), tr("Notation musicale : Cm7, G7, Fsus4…"),
                             juce::MessageBoxIconType::NoIcon, this);
    dialog.setOpaque(false);
    roundNativeWindow(dialog);
    dialog.addTextEditor("name", text(c, "name"), tr("Accord"));
    dialog.addTextEditor("bass", text(c, "bass"), tr("Basse / renversement"));
    dialog.addButton("Appliquer", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog.addButton(tr("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    if (dialog.runModalLoop() != 1)
        return;
    auto name = dialog.getTextEditorContents("name").trim(),
         bass = dialog.getTextEditorContents("bass").trim();
    if (name != "N" && pitch(name) < 0) {
        message(*this, tr("Accord"), tr("Fondamentale invalide."));
        return;
    }
    snapshot();
    c["name"] = utf(name);
    c["bass"] = utf(bass);
    c["notes"] = theory.notes(name, bass);
    c["source"] = juce::String::fromUTF8("édition");
    refresh();
}
void App::splitChord() {
    if (jobActive)
        return;
    double t = audio.seconds();
    for (size_t i = 0; i < doc.chords().size(); ++i) {
        auto c = doc.chords()[i];
        if (c.at("start").get<double>() + .03 < t && t < c.at("end").get<double>() - .03) {
            snapshot();
            doc.chords()[i]["end"] = t;
            c["start"] = t;
            doc.chords().insert(doc.chords().begin() + (J::difference_type)i + 1, c);
            refresh();
            select((int)i + 1);
            return;
        }
    }
}
void App::mergeChord() {
    if (jobActive || selected < 0 || selected + 1 >= (int)doc.chords().size())
        return;
    snapshot();
    doc.chords()[(size_t)selected]["end"] = doc.chords()[(size_t)selected + 1].at("end");
    doc.chords().erase(doc.chords().begin() + selected + 1);
    refresh();
}
void App::clearChord() {
    if (jobActive || selected < 0)
        return;
    snapshot();
    auto &c = doc.chords()[(size_t)selected];
    c["name"] = "N";
    c["bass"] = tr("");
    c["notes"] = J::array();
    c["source"] = juce::String::fromUTF8("édition");
    refresh();
}
void App::editLyric(int row) {
    if (jobActive || row < 0 || row >= (int)doc.lyrics().size())
        return;
    if (text(doc.j, "reference_text").isNotEmpty()) {
        alignLyrics();
        return;
    }
    String value = text(doc.lyrics()[row], "text");
    textDialog(*this, tr("Modifier les paroles"),
               tr("Une modification du texte retire les horodatages des mots de cette ligne."), value, false,
               false, [safe = safePointer(), row](int code, String value) {
                   if (!safe || !code || row >= (int)safe->doc.lyrics().size())
                       return;
                   safe->snapshot();
                   if (value.trim().isEmpty())
                       safe->doc.lyrics().erase(safe->doc.lyrics().begin() + row);
                   else if (value != text(safe->doc.lyrics()[row], "text")) {
                       safe->doc.lyrics()[row]["text"] = utf(value);
                       safe->doc.lyrics()[row]["words"] = J::array();
                   }
                   safe->refresh();
               });
}

void App::marker() {
    juce::AlertWindow dialog(tr("Repère"), tr("Nom du repère à la tête de lecture"),
                             juce::MessageBoxIconType::NoIcon, this);
    dialog.setOpaque(false);
    roundNativeWindow(dialog);
    dialog.addTextEditor("name", tr(""), tr("Nom"));
    dialog.addButton("Ajouter", 1);
    dialog.addButton(tr("Annuler"), 0);
    if (dialog.runModalLoop() == 1 && dialog.getTextEditorContents("name").trim().isNotEmpty()) {
        snapshot();
        doc.j["markers"].push_back(
            {{"time", audio.seconds()}, {"name", utf(dialog.getTextEditorContents("name").trim())}});
        timeline.repaint();
    }
}
void App::audioSettings() {
    if (audio.isRecording()) {
        setStatus(tr("Terminer l’enregistrement avant de changer de matériel."));
        return;
    }
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(audio.devices, 0, 2, 0, 2, false,
                                                                         false, true, false);
    presentDialog(tr("Entrées et sorties audio"), std::move(selector), 680, 480,
                  [safe = safePointer()](int, juce::Component &) {
                      if (safe)
                          safe->savePreferences();
                  });
}

void App::about() {
    struct About : juce::Component {
        juce::Image image;
        juce::Label text;
        juce::TextButton license{tr("Licence de KITHÁRA NÉA")};
        About(App &app, juce::Image i) : image(i) {
            addAndMakeVisible(license);
            license.onClick = [&app] { app.ownLicense(); };
            addAndMakeVisible(text);
            text.setText(
                tr("Étudier les accords, suivre les paroles et préparer une song sheet.\nTraitements locaux. "
                   "Projets .knea autonomes ou légers.\nApplication C++ / JUCE."),
                juce::dontSendNotification);
            text.setJustificationType(juce::Justification::centred);
            setSize(500, 480);
        }
        void resized() override {
            text.setBounds(20, 342, getWidth() - 40, 85);
            license.setBounds(85, getHeight() - 46, getWidth() - 170, 32);
        }
        void paint(juce::Graphics &g) override {
            g.drawImageWithin(image, 80, 10, getWidth() - 160, 330, juce::RectanglePlacement::centred);
        }
    };
    presentDialog(tr("À propos de KITHÁRA NÉA"), std::make_unique<About>(*this, logo), 520, 520);
}

void App::shortcuts() {
    message(
        *this, tr("Raccourcis clavier"),
        isEnglish()
            ? String::fromUTF8(
                  "Space: play / pause; finish recording · R: record / stop\nSpace / Esc: cancel recording countdown\nEsc: cancel current processing\nCtrl+O: open "
                  "audio · Ctrl+Shift+O: open project\nCtrl+S: save · Ctrl+Shift+S: save as\nCtrl+Z / "
                  "Ctrl+Y: undo / redo\nCtrl+Left / Ctrl+Right: back / forward 5 seconds\nCtrl+Home / "
                  "Ctrl+End: beginning / end\nEnter: edit chord · Delete: no chord\nCtrl+K: split · Ctrl+M: "
                  "merge · Ctrl+B: marker\nHome: readable waveform zoom\nWheel: waveform zoom · Middle drag: "
                  "pan · Shift drag: loop\nNavigation arrows: click ±5 s; double-click beginning / "
                  "end\nDouble-click the left track labels: return to beginning\nEdit data: change exact "
                  "event timestamps")
            : String::fromUTF8(
                  "Espace : lecture / pause ; terminer la prise · R : enregistrer / terminer\nEspace / Échap : annuler le décompte\nÉchap : annuler le traitement en "
                  "cours\nCtrl+O : ouvrir un audio · Ctrl+Maj+O : ouvrir un projet\nCtrl+S : enregistrer · "
                  "Ctrl+Maj+S : enregistrer sous\nCtrl+Z / Ctrl+Y : annuler / rétablir\nCtrl+← / Ctrl+→ : "
                  "reculer / avancer de 5 secondes\nCtrl+Origine / Ctrl+Fin : début / fin\nEntrée : modifier "
                  "l’accord · Suppr : sans accord\nCtrl+K : scinder · Ctrl+M : fusionner · Ctrl+B : "
                  "repère\nOrigine : zoom lisible de la forme d’onde\nMolette : zoom de l’onde · Clic "
                  "molette : déplacer · Maj + glisser : boucle\nFlèches : clic ±5 s ; double-clic début / "
                  "fin\nDouble-clic sur les libellés des pistes à gauche : revenir au début\nÉdition des "
                  "données : corriger les horodatages exacts"));
}
void App::syncZoom() {
    double maximum = std::max(30., doc.duration()), minimum = std::min(.001, timeline.readableSpan());
    waveZoom.setValue(
        std::clamp(std::log(std::max(minimum, timeline.span) / maximum) / std::log(minimum / maximum), 0.,
                   1.),
        juce::dontSendNotification);
}
void App::resetView() { timeline.applyReadableZoom(); }
} // namespace kn
