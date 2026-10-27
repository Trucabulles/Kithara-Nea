#include "UI.h"
#include <dwmapi.h>
namespace kn {
void roundNativeWindow(juce::Component &component) {
    if (!component.isOnDesktop() || !component.getPeer() || component.getWidth() <= 0)
        return;
    auto handle = (HWND)component.getPeer()->getNativeHandle();
    RECT rect{};
    if (!GetWindowRect(handle, &rect))
        return;
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    auto key = String((int64_t)(intptr_t)handle) + ":" + String(width) + ":" + String(height);
    if (component.getProperties()["roundedNativeSize"].toString() == key)
        return;
    component.getProperties().set("roundedNativeSize", key);
    // Use the same software renderer as the main window. The accelerated backend
    // can omit bitmap images on a transparent owned window on Windows.
    auto *peer = component.getPeer();
    const auto engines = peer->getAvailableRenderingEngines();
    for (int i = 0; i < engines.size(); ++i)
        if (engines[i].containsIgnoreCase("software") && peer->getCurrentRenderingEngine() != i) {
            peer->setCurrentRenderingEngine(i);
            break;
        }
    int corners = 2;
    DwmSetWindowAttribute(handle, 33, &corners, sizeof(corners));
}
static void attachOwner(juce::DialogWindow &window, juce::Component &owner) {
    if (!window.getPeer() || !owner.getPeer())
        return;
    auto child = (HWND)window.getPeer()->getNativeHandle();
    auto parent = (HWND)owner.getPeer()->getNativeHandle();
    SetWindowLongPtrW(child, GWLP_HWNDPARENT, (LONG_PTR)parent);
    auto style = GetWindowLongPtrW(child, GWL_EXSTYLE);
    SetWindowLongPtrW(child, GWL_EXSTYLE, (style & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW);
    roundNativeWindow(window);
}
DataWindow::DataWindow(App &owner)
    : CloseableDialog(tr("Édition des données"), owner.theme.paper), app(owner) {
    setLookAndFeel(&app.theme);
    setContentNonOwned(&app.tabs, false);
    setResizable(true, false);
    app.tabs.setVisible(true);
    centreAroundComponent(&app, 980, 440);
    setResizeLimits(620, 260, 2400, 1400);
    attachOwner(*this, app);
}
DataWindow::~DataWindow() {
    clearContentComponent();
    setLookAndFeel(nullptr);
}
bool DataWindow::keyPressed(const juce::KeyPress &key) {
    if (key == juce::KeyPress::escapeKey) {
        closeButtonPressed();
        return true;
    }
    return app.keyPressed(key);
}
namespace {
class LicenseView : public juce::Component {
    struct Notice {
        String title;
        File file;
    };
    std::vector<Notice> notices;
    juce::ComboBox list;
    juce::TextEditor body;
    juce::Label heading;

  public:
    juce::TextButton close{tr("Fermer")};
    explicit LicenseView(App &app) {
        setLookAndFeel(&app.theme);
        for (auto *c : std::initializer_list<juce::Component *>{&heading, &list, &body, &close})
            addAndMakeVisible(c);
        heading.setText(tr("Composants, auteurs et textes complets des licences"),
                        juce::dontSendNotification);
        body.setMultiLine(true, false);
        body.setReadOnly(true);
        body.setFont(juce::FontOptions("Segoe UI", 15, juce::Font::plain));
        body.setScrollbarsShown(true);
        body.setColour(juce::TextEditor::textColourId, app.theme.ink);
        body.setColour(juce::TextEditor::backgroundColourId, app.theme.control);
        list.addItem(tr("Présentation des composants"), 1);
        const std::vector<std::pair<String, String>> groups = {
            {"notices", tr("Bibliothèques")},
            {"independent/theory/notices", tr("Théorie musicale et accordages")},
            {"independent/sounds/notices", tr("Banques sonores")},
            {"lv_chordia_native/notices", tr("Analyse harmonique")},
            {"whisper/licenses", "Transcription"},
            {"models/beat_this_official", tr("Analyse rythmique")}};
        for (auto &group : groups) {
            auto files = resource(group.first).findChildFiles(File::findFiles, true, "*");
            std::sort(files.begin(), files.end(),
                      [](const File &a, const File &b) { return a.getFullPathName() < b.getFullPathName(); });
            for (auto &file : files) {
                if (group.first == "notices" && file.getParentDirectory() != resource("notices") &&
                    !file.isAChildOf(resource("notices/fluidsynth")) &&
                    !file.isAChildOf(resource("notices/libsndfile")) &&
                    !file.isAChildOf(resource("notices/ffmpeg")))
                    continue;
                auto name = file.getFileName().toLowerCase();
                if (file.getSize() > 1000000 || !(file.hasFileExtension("txt;md") || name == "license" ||
                                                  name == "copying" || name == "notice"))
                    continue;
                notices.push_back({group.second + juce::String::fromUTF8(" · ") +
                                       file.getParentDirectory().getFileName() +
                                       juce::String::fromUTF8(" · ") + file.getFileNameWithoutExtension(),
                                   file});
                list.addItem(notices.back().title, (int)notices.size() + 1);
            }
        }
        list.onChange = [this] { show(list.getSelectedId()); };
        list.setSelectedId(1, juce::dontSendNotification);
        show(1);
        setSize(900, 620);
    }
    void show(int id) {
        if (id < 1 || id > (int)notices.size() + 1)
            return;
        String content;
        if (id == 1)
            content =
                isEnglish()
                    ? String::fromUTF8(
                          "KITHÁRA NÉA — Credits and third-party licenses\n\nJUCE — Raw Material Software "
                          "Limited. JUCE license.\nLV-Chordia — OpenMIRLab and contributors. MIT.\nBeat This "
                          "small0 — CPJKU and contributors. MIT; official model converted to ONNX.\nONNX "
                          "Runtime — Microsoft and contributors. MIT.\nWhisper — OpenAI. MIT. whisper.cpp — "
                          "Georgi Gerganov and contributors. MIT.\nVulkan headers / Vulkan-Hpp — Khronos Group and contributors. MIT / Apache-2.0.\nFluidSynth — authors and contributors. "
                          "LGPL.\nFFmpeg — authors and contributors. LGPL-2.1-or-later; audio-focused build.\nmusic21 "
                          "— authors and contributors. BSD-3-Clause.\nTuneStand — CC BY 4.0; String Tunings "
                          "— MIT.\nFreePats sound libraries: CC0, except the steel guitar under GPL with "
                          "exception.\n\nChoose a notice to read its full original text, authors and "
                          "attributions.")
                    : String::fromUTF8(
                          "KITHÁRA NÉA — Crédits et licences tierces\n\nJUCE — Raw Material Software "
                          "Limited. Licence JUCE ou AGPLv3.\nLV-Chordia — OpenMIRLab et contributeurs. "
                          "MIT.\nBeat This small0 — CPJKU et contributeurs. MIT ; modèle officiel converti "
                          "en ONNX.\nONNX Runtime — Microsoft et contributeurs. MIT.\nWhisper — OpenAI. MIT. "
                          "whisper.cpp — Georgi Gerganov et contributeurs. MIT.\nVulkan headers / Vulkan-Hpp — Khronos Group et contributeurs. MIT / Apache-2.0.\nFluidSynth — auteurs et "
                          "contributeurs. LGPL.\nFFmpeg — auteurs et contributeurs. LGPL-2.1-ou-ultérieure ; "
                          "compilation pour l’audio.\nmusic21 — auteurs et contributeurs. BSD-3-Clause.\nTuneStand — CC BY "
                          "4.0 ; String Tunings — MIT.\nBanques sonores FreePats : CC0, sauf guitare acier "
                          "sous GPL avec exception.\n\nChoisir une notice dans la liste pour consulter son "
                          "texte intégral, ses auteurs et ses attributions.");
        else
            content = notices[(size_t)id - 2].file.loadFileAsString();
        body.setText(content, false);
        body.setCaretPosition(0);
    }
    int noticeCount() const { return (int)notices.size(); }
    String text() const { return body.getText(); }
    void resized() override {
        auto r = getLocalBounds().reduced(18);
        heading.setBounds(r.removeFromTop(26));
        r.removeFromTop(8);
        list.setBounds(r.removeFromTop(32));
        r.removeFromTop(12);
        auto footer = r.removeFromBottom(36);
        close.setBounds(footer.removeFromRight(100));
        r.removeFromBottom(10);
        body.setBounds(r);
    }
    void paint(juce::Graphics &g) override {
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
    }
};
} // namespace
CloseableDialog *App::presentDialog(String title, std::unique_ptr<juce::Component> content, int width,
                                    int height, std::function<void(int, juce::Component &)> completed) {
    auto window = std::make_unique<CloseableDialog>(title, theme.paper);
    auto *ptr = window.get();
    window->setLookAndFeel(&theme);
    window->setContentOwned(content.release(), true);
    window->setResizable(true, false);
    window->centreAroundComponent(this, width, height);
    auto safe = safePointer();
    auto weak = juce::Component::SafePointer<CloseableDialog>(ptr);
    ptr->onResult = [safe, weak, completed](int code) {
        if (!safe || !weak)
            return;
        if (completed && weak->getContentComponent())
            completed(code, *weak->getContentComponent());
        juce::MessageManager::callAsync([safe, weak] {
            if (!safe)
                return;
            auto &windows = safe->dialogs;
            windows.erase(std::remove_if(windows.begin(), windows.end(),
                                         [weak](auto &w) { return w.get() == weak.getComponent(); }),
                          windows.end());
        });
    };
    dialogs.push_back(std::move(window));
    ptr->setVisible(true);
    attachOwner(*ptr, *this);
    ptr->enterModalState(true, nullptr, false);
    if (auto *b = ptr->getCloseButton())
        b->setEnabled(true);
    return ptr;
}
CloseableDialog *App::activeDialog() const {
    for (auto it = dialogs.rbegin(); it != dialogs.rend(); ++it)
        if ((*it)->isVisible())
            return it->get();
    return nullptr;
}
void App::licenses() {
    auto content = std::make_unique<LicenseView>(*this);
    auto *controls = content.get();
    auto *d = presentDialog(tr("Crédits et licences tierces"), std::move(content), 940, 690);
    auto safe = juce::Component::SafePointer<CloseableDialog>(d);
    controls->close.onClick = [safe] {
        if (safe)
            safe->closeButtonPressed();
    };
}
bool App::applyTimes(int row, bool lyrics, double from, double to, String &error) {
    auto &events = lyrics ? doc.lyrics() : doc.chords();
    if (jobActive || row < 0 || row >= (int)events.size()) {
        error = tr("Aucun événement modifiable.");
        return false;
    }
    if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to <= from || to > doc.duration() + 1e-6) {
        error = tr("Les temps doivent rester dans la durée du morceau, avec fin supérieure au début.");
        return false;
    }
    if ((row > 0 && from < events[(size_t)row - 1].at("end").get<double>() - 1e-6) ||
        (row + 1 < (int)events.size() && to > events[(size_t)row + 1].at("start").get<double>() + 1e-6)) {
        error = tr("Ces temps chevauchent un événement voisin.");
        return false;
    }
    if (lyrics)
        for (auto &word : words(events[(size_t)row]))
            if (word.at("start").get<double>() < from || word.at("end").get<double>() > to) {
                error = tr("Conserver les mots horodatés à l’intérieur de la ligne.");
                return false;
            }
    const auto previous = doc;
    snapshot();
    events[(size_t)row]["start"] = from;
    events[(size_t)row]["end"] = to;
    updateAutomaticSheet(doc, previous);
    refresh();
    seek(from);
    error = {};
    return true;
}
namespace {
std::optional<double> dataNumber(String input) {
    auto value = utf(input.trim().replaceCharacter(',', '.'));
    try {
        size_t count = 0;
        const double n = std::stod(value, &count);
        if (count == value.size() && std::isfinite(n))
            return n;
    } catch (...) {
    }
    return {};
}
void syncReferenceLine(Document &candidate, const Document &previous, int row) {
    const auto source = text(previous.j, "reference_text");
    if (source.isEmpty())
        return;
    const auto &oldCue = previous.lyrics()[(size_t)row];
    const auto oldText = text(oldCue, "text");
    int base = -1;
    for (const auto &word : oldCue.value("words", J::array())) {
        for (const auto &match : previous.j.at("reference_matches"))
            if (match.at("start") == word.at("start") && match.at("end") == word.at("end") &&
                text(match, "text") == text(word, "text")) {
                base = match.at("offset").get<int>() - word.at("offset").get<int>();
                break;
            }
        if (base >= 0)
            break;
    }
    if (base < 0) {
        for (int offset = 0; offset <= source.length();) {
            int end = source.indexOfChar(offset, '\n');
            if (end < 0)
                end = source.length();
            if (source.substring(offset, end) == oldText) {
                require(base < 0, tr("Ligne ambiguë dans le texte source : utiliser l’éditeur de paroles."));
                base = offset;
            }
            if (end == source.length())
                break;
            offset = end + 1;
        }
    }
    require(base >= 0 && source.substring(base, base + oldText.length()) == oldText,
            tr("Ligne introuvable dans le texte source : utiliser l’éditeur de paroles."));
    const auto &cue = candidate.lyrics()[(size_t)row];
    const auto newText = text(cue, "text");
    const int delta = newText.length() - oldText.length();
    candidate.j["reference_text"] =
        utf(source.substring(0, base) + newText + source.substring(base + oldText.length()));
    J matches = J::array();
    for (auto match : previous.j.at("reference_matches")) {
        int offset = match.at("offset");
        if (offset >= base && offset < base + oldText.length())
            continue;
        if (offset >= base + oldText.length())
            match["offset"] = offset + delta;
        matches.push_back(match);
    }
    for (const auto &word : cue.value("words", J::array()))
        matches.push_back({{"start", word.at("start")},
                           {"end", word.at("end")},
                           {"text", word.at("text")},
                           {"offset", base + word.at("offset").get<int>()},
                           {"kind", "exact"}});
    std::sort(matches.begin(), matches.end(),
              [](const J &a, const J &b) { return a.at("offset").get<int>() < b.at("offset").get<int>(); });
    candidate.j["reference_matches"] = matches;
}
} // namespace
bool App::applyDataCell(int row, bool lyrics, int column, String value, String &error) {
    if (jobActive || row < 0 || row >= (int)(lyrics ? doc.lyrics().size() : doc.chords().size())) {
        error = tr("Aucun événement modifiable.");
        return false;
    }
    const auto previous = doc;
    const auto &current = (lyrics ? doc.lyrics() : doc.chords())[(size_t)row];
    if (column == 1 || column == 2) {
        auto n = dataNumber(value);
        if (!n) {
            error = tr("Saisir deux nombres valides.");
            return false;
        }
        return applyTimes(row, lyrics, column == 1 ? *n : current.at("start").get<double>(),
                          column == 2 ? *n : current.at("end").get<double>(), error);
    }
    try {
        auto candidate = doc;
        auto &event = (lyrics ? candidate.lyrics() : candidate.chords())[(size_t)row];
        if (lyrics && column == 3) {
            require(value.isNotEmpty() && !value.containsChar('\n') && !value.containsChar('\r'),
                    tr("Saisir une seule ligne non vide."));
            if (value == text(event, "text")) {
                error = {};
                return true;
            }
            event["text"] = utf(value);
            event["words"] = J::array();
            syncReferenceLine(candidate, previous, row);
        } else if (!lyrics && (column == 3 || column == 4)) {
            value = value.trim();
            const int transposition = candidate.j.value("transpose", 0);
            if (column == 3) {
                String bass;
                auto name = value;
                const int slash = name.lastIndexOfChar('/');
                static const std::regex notePattern("[A-G](?:#|b)?");
                if (slash >= 0 && std::regex_match(utf(name.substring(slash + 1)), notePattern)) {
                    bass = name.substring(slash + 1);
                    name = name.substring(0, slash);
                    event["bass"] = utf(transpose(bass, -transposition));
                }
                require(name == "N" || pitch(name) >= 0, tr("Fondamentale invalide."));
                if (name != "N") {
                    const int rootLength = name.length() > 1 && (name[1] == '#' || name[1] == 'b') ? 2 : 1;
                    theory.intervals(name.substring(rootLength).replace("6/9", "69"));
                }
                event["name"] = utf(transpose(name, -transposition));
                if (name == "N")
                    event["bass"] = "";
            } else {
                require(value.isEmpty() || std::regex_match(utf(value), std::regex("[A-G](?:#|b)?")),
                        tr("Basse invalide."));
                event["bass"] = utf(transpose(value, -transposition));
            }
            event["notes"] = theory.notes(text(event, "name"), text(event, "bass"));
            event["source"] = "manual";
        } else
            return false;
        candidate.validate();
        updateAutomaticSheet(candidate, previous);
        snapshot();
        doc = std::move(candidate);
        refresh();
        setStatus(lyrics ? tr("Ligne corrigée ; ses anciens temps de mots ont été retirés.")
                         : tr("Correction enregistrée"));
        error = {};
        return true;
    } catch (const std::exception &e) {
        error = String::fromUTF8(e.what());
        return false;
    }
}
bool App::applyWordCell(int row, int wordIndex, int column, String value, String &error) {
    if (jobActive || row < 0 || row >= (int)doc.lyrics().size()) {
        error = tr("Aucun événement modifiable.");
        return false;
    }
    try {
        const auto previous = doc;
        auto candidate = doc;
        auto &cue = candidate.lyrics()[(size_t)row];
        auto &words = cue["words"];
        require(wordIndex >= 0 && wordIndex < (int)words.size(), tr("Aucun événement modifiable."));
        auto &word = words[(size_t)wordIndex];
        if (column == 1 || column == 2) {
            auto number = dataNumber(value);
            require(number.has_value(), tr("Saisir deux nombres valides."));
            word[column == 1 ? "start" : "end"] = *number;
        } else {
            value = value.trim();
            require(value.isNotEmpty() && !value.containsAnyOf(" \t\r\n"),
                    tr("Saisir un seul mot non vide."));
            const int offset = word.at("offset"), length = text(word, "text").length();
            const auto line = text(cue, "text");
            cue["text"] = utf(line.substring(0, offset) + value + line.substring(offset + length));
            word["text"] = utf(value);
            for (size_t i = wordIndex + 1; i < words.size(); ++i)
                words[i]["offset"] = words[i].at("offset").get<int>() + value.length() - length;
        }
        const double start = word.at("start"), end = word.at("end");
        require(start >= cue.at("start").get<double>() && end <= cue.at("end").get<double>() &&
                    end >= start &&
                    (wordIndex == 0 || start >= words[(size_t)wordIndex - 1].at("end").get<double>()) &&
                    (wordIndex + 1 == (int)words.size() ||
                     end <= words[(size_t)wordIndex + 1].at("start").get<double>()),
                tr("Les temps du mot doivent rester dans la ligne, sans chevaucher ses voisins."));
        syncReferenceLine(candidate, previous, row);
        candidate.validate();
        updateAutomaticSheet(candidate, previous);
        snapshot();
        doc = std::move(candidate);
        refresh();
        setStatus(tr("Correction enregistrée"));
        error = {};
        return true;
    } catch (const std::exception &e) {
        error = String::fromUTF8(e.what());
        return false;
    }
}
void App::editWords(int row) {
    if (jobActive || row < 0 || row >= (int)doc.lyrics().size())
        return;
    struct Content : juce::Component, juce::TableListBoxModel {
        App &app;
        int row;
        juce::Label hint;
        juce::TableListBox table;
        juce::TextButton close{tr("Fermer")};
        Content(App &a, int r) : app(a), row(r) {
            for (auto *c : std::initializer_list<juce::Component *>{&hint, &table, &close})
                addAndMakeVisible(c);
            hint.setText(tr("Double-cliquer pour modifier. Entrée : valider ; Échap : annuler."),
                         juce::dontSendNotification);
            hint.setFont(juce::FontOptions(16));
            table.setModel(this);
            table.setRowHeight(32);
            table.getHeader().addColumn(tr("Début"), 1, 120);
            table.getHeader().addColumn(tr("Fin"), 2, 120);
            table.getHeader().addColumn(tr("Mot"), 3, 340);
        }
        ~Content() override { table.setModel(nullptr); }
        int getNumRows() override {
            return row < (int)app.doc.lyrics().size()
                       ? (int)app.doc.lyrics()[row].value("words", J::array()).size()
                       : 0;
        }
        void paintRowBackground(juce::Graphics &g, int, int, int, bool) override {
            g.fillAll(app.theme.control);
        }
        void paintCell(juce::Graphics &, int, int, int, int, bool) override {}
        juce::Component *refreshComponentForCell(int index, int column, bool,
                                                 juce::Component *existing) override {
            auto *label = dynamic_cast<juce::Label *>(existing);
            if (!label) {
                delete existing;
                label = new juce::Label();
            }
            const auto &word = app.doc.lyrics()[row]["words"][index];
            label->onTextChange = nullptr;
            label->setText(column == 3 ? text(word, "text")
                                       : String(word.at(column == 1 ? "start" : "end").get<double>(), 3),
                           juce::dontSendNotification);
            label->setFont(juce::FontOptions(17));
            label->setEditable(false, true, false);
            label->onTextChange = [safe = juce::Component::SafePointer<Content>(this),
                                   weak = juce::Component::SafePointer<juce::Label>(label), index, column] {
                const String value = weak ? weak->getText() : String();
                juce::MessageManager::callAsync([safe, index, column, value] {
                    if (!safe)
                        return;
                    String error;
                    if (!safe->app.applyWordCell(safe->row, index, column, value, error))
                        safe->hint.setText(error, juce::dontSendNotification);
                    else
                        safe->hint.setText(tr("Correction enregistrée"), juce::dontSendNotification);
                    safe->table.updateContent();
                    safe->table.repaint();
                });
            };
            return label;
        }
        void resized() override {
            auto r = getLocalBounds().reduced(16);
            hint.setBounds(r.removeFromTop(52));
            close.setBounds(r.removeFromBottom(34).removeFromRight(110));
            r.removeFromBottom(10);
            table.setBounds(r);
            table.getHeader().setColumnWidth(3, std::max(180, r.getWidth() - 240));
        }
    };
    auto content = std::make_unique<Content>(*this, row);
    auto *controls = content.get();
    auto *dialog = presentDialog(tr("Modifier les mots horodatés"), std::move(content), 680, 430);
    controls->close.onClick = [weak = juce::Component::SafePointer<CloseableDialog>(dialog)] {
        if (weak)
            weak->closeButtonPressed();
    };
}
void App::editTime(int row, bool lyrics) {
    auto &events = lyrics ? doc.lyrics() : doc.chords();
    if (jobActive || row < 0 || row >= (int)events.size())
        return;
    struct Content : juce::Component {
        juce::Label hint, a, b;
        juce::TextEditor from, to;
        juce::TextButton save{"Appliquer"}, cancel{tr("Annuler")};
        Content(double start, double end) {
            for (auto *c :
                 std::initializer_list<juce::Component *>{&hint, &a, &b, &from, &to, &save, &cancel})
                addAndMakeVisible(c);
            hint.setText(tr("Horodatages en secondes. Les voisins et les mots existants sont préservés."),
                         juce::dontSendNotification);
            a.setText(tr("Début"), juce::dontSendNotification);
            b.setText(tr("Fin"), juce::dontSendNotification);
            from.setText(String(start, 3));
            to.setText(String(end, 3));
            setSize(560, 220);
        }
        void resized() override {
            hint.setBounds(18, 12, getWidth() - 36, 48);
            a.setBounds(18, 75, 90, 28);
            from.setBounds(115, 75, 160, 28);
            b.setBounds(18, 116, 90, 28);
            to.setBounds(115, 116, 160, 28);
            save.setBounds(getWidth() - 240, 174, 105, 30);
            cancel.setBounds(getWidth() - 125, 174, 105, 30);
        }
    };
    auto content = std::make_unique<Content>(events[row].at("start"), events[row].at("end"));
    auto *controls = content.get();
    auto *dialog = presentDialog(tr("Modifier les horodatages"), std::move(content), 590, 260);
    auto weak = juce::Component::SafePointer<CloseableDialog>(dialog);
    controls->save.onClick = [this, controls, weak, row, lyrics] {
        auto parse = [](String input) -> std::optional<double> {
            auto value = utf(input.trim().replaceCharacter(',', '.'));
            try {
                size_t count = 0;
                double n = std::stod(value, &count);
                if (count == value.size() && std::isfinite(n))
                    return n;
            } catch (...) {
            }
            return {};
        };
        auto from = parse(controls->from.getText()), to = parse(controls->to.getText());
        String error;
        if (!from || !to) {
            controls->hint.setText(tr("Saisir deux nombres valides."), juce::dontSendNotification);
            return;
        }
        if (applyTimes(row, lyrics, *from, *to, error))
            weak->exitModalState(1);
        else
            controls->hint.setText(error, juce::dontSendNotification);
    };
    controls->cancel.onClick = [weak] {
        if (weak)
            weak->closeButtonPressed();
    };
}

J verifyDialogs(App &app, const File &output) {
    J result = J::array();
    auto test = [&](String title, auto open) {
        open();
        auto *dialog = app.activeDialog();
        require(dialog && dialog->getName() == title, "Dialogue absent : " + title);
        if (auto *peer = dialog->getPeer())
            require(
                peer->getAvailableRenderingEngines()[peer->getCurrentRenderingEngine()].containsIgnoreCase(
                    "software"),
                "Moteur de rendu du dialogue different : " + title);
        auto weak = juce::Component::SafePointer<CloseableDialog>(dialog);
        auto image = dialog->createComponentSnapshot(dialog->getLocalBounds());
        auto pngFile =
            output.getChildFile("dialog-" + String((int)result.size()) + ".png").createOutputStream();
        juce::PNGImageFormat().writeImageToStream(image, *pngFile);
        require(image.getPixelAt(0, 0).getAlpha() == 0, "Coin de dialogue opaque : " + title);
        require(dialog->getContentComponent()->getX() >= 12, "Contenu empiete sur les coins : " + title);
        dialog->getCloseButton()->triggerClick();
        auto end = juce::Time::getMillisecondCounterHiRes() + 2000;
        while (weak && weak->isVisible() && juce::Time::getMillisecondCounterHiRes() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        require(!weak || !weak->isVisible(), "Croix inactive : " + title);
        result.push_back(utf(title));
    };
    test(juce::String::fromUTF8("À propos de KITHÁRA NÉA"), [&] { app.about(); });
    test(juce::String::fromUTF8("Entrées et sorties audio"), [&] { app.audioSettings(); });
    test(juce::String::fromUTF8("Crédits et licences tierces"), [&] { app.licenses(); });
    test(juce::String::fromUTF8("Song sheet · paroles et accords"), [&] { app.editSheet(); });
    test("Modifier les horodatages", [&] { app.editTime(0, false); });
    {
        std::unique_ptr<juce::AlertWindow> alert(app.theme.createAlertWindow(
            tr("Projet modifié"), tr("Enregistrer les changements du projet ?"), tr("Enregistrer"),
            tr("Ignorer"), tr("Annuler"), juce::MessageBoxIconType::NoIcon, 3, &app));
        auto image = alert->createComponentSnapshot(alert->getLocalBounds());
        auto png = output.getChildFile("dialog-confirmation.png").createOutputStream();
        juce::PNGImageFormat().writeImageToStream(image, *png);
        require(!alert->isOpaque() && image.getPixelAt(0, 0).getAlpha() == 0, "Confirmation non arrondie");
    }
    CloseableDialog dialog("Test Escape", app.theme.paper);
    dialog.setSize(250, 120);
    dialog.setVisible(true);
    dialog.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    require(!dialog.isVisible(), "Echap ne ferme pas la fenetre");
    LicenseView licenses(app);
    require(licenses.noticeCount() > 5, "Notices manquantes");
    require(!licenses.text().contains("C:\\") && !licenses.text().contains("resources/"),
            "Chemin local dans la presentation");
    licenses.show(2);
    require(licenses.text().isNotEmpty(), "Notice vide");
    writeJson(output.getChildFile("dialogs.json"), result);
    return result;
}
} // namespace kn
