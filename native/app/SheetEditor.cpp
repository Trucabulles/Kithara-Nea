#include "UI.h"
namespace kn {
static float textWidth(const juce::Font &font, const String &text) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0, 0);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}
class SheetPreview : public juce::Component {
    friend void verifySheetEditor(App &, const File &);
    App &app;
    struct Diagram {
        String name;
        juce::Rectangle<float> rect;
    };
    std::vector<Diagram> diagrams;
    struct Drawn {
        juce::Rectangle<float> lyrics, chord;
        SheetToken token;
    };
    std::vector<Drawn> drawn;
    String source;
    int needed = 100;
    bool rebuilding = false;
    String dragName, dragSource;
    juce::Point<float> dragPoint, dropPoint;
    int dragStart = -1, dragEnd = -1, dropAt = -1;
    int hoverStart = -1;
    juce::Rectangle<float> chordBounds(const Drawn &item) const {
        return item.chord.withWidth(std::min(
            item.chord.getWidth(),
            textWidth(juce::Font(juce::FontOptions("Segoe UI", 16, juce::Font::bold)), item.token.chord) +
                4));
    }
    int insertionAt(juce::Point<float> point, juce::Point<float> *marker = nullptr) const {
        for (const auto &item : drawn)
            if (item.lyrics.contains(point)) {
                auto font = juce::Font(juce::FontOptions("Segoe UI", 17, juce::Font::plain));
                int index = 0;
                while (index < item.token.text.length() &&
                       point.x - item.lyrics.getX() >
                           textWidth(font, item.token.text.substring(0, index + 1)) -
                               textWidth(font, item.token.text.substring(index, index + 1)) * .5f)
                    ++index;
                if (marker)
                    *marker = {item.lyrics.getX() + textWidth(font, item.token.text.substring(0, index)),
                               item.lyrics.getY()};
                return item.token.sourceStart + index;
            }
        return -1;
    }
    void cancelDrag() {
        dragStart = dragEnd = dropAt = -1;
        dragName.clear();
        repaint();
    }
    void rebuild() {
        if (rebuilding)
            return;
        rebuilding = true;
        drawn.clear();
        diagrams.clear();
        std::vector<String> used;
        float y = 15;
        juce::Font lyricFont(juce::FontOptions("Segoe UI", 17, juce::Font::plain)),
            chordFont(juce::FontOptions("Segoe UI", 16, juce::Font::bold));
        for (auto &row : sheetLines(source)) {
            const bool hasChord = std::any_of(row.tokens.begin(), row.tokens.end(),
                                              [](const auto &token) { return token.chord.isNotEmpty(); });
            const bool hasText = std::any_of(row.tokens.begin(), row.tokens.end(), [](const auto &token) {
                return token.text.trim().isNotEmpty();
            });
            const float rowHeight = hasChord && hasText ? 46.f : 24.f;
            const float chordHeight = hasChord && hasText ? 20.f : 0.f;
            if (!hasChord && !hasText) {
                y += rowHeight;
                continue;
            }
            float x = 15;
            for (auto &token : row.tokens) {
                if (token.chord.isNotEmpty() &&
                    std::find(used.begin(), used.end(), token.chord) == used.end())
                    used.push_back(token.chord);
                String remaining = token.text;
                int offset = 0;
                bool first = true;
                do {
                    int stop = offset;
                    while (stop < remaining.length() &&
                           !juce::CharacterFunctions::isWhitespace(remaining[stop]))
                        ++stop;
                    while (stop < remaining.length() &&
                           juce::CharacterFunctions::isWhitespace(remaining[stop]))
                        ++stop;
                    auto value = remaining.substring(offset, stop);
                    auto name = first ? token.chord : String();
                    float width =
                        std::max(3.f, std::max(textWidth(lyricFont, value),
                                               name.isEmpty() ? 0.f : textWidth(chordFont, name) + 8));
                    if (x + width > getWidth() - 15 && x > 15) {
                        x = 15;
                        y += rowHeight;
                    }
                    SheetToken piece{value, name, token.sourceStart + offset, token.sourceStart + stop};
                    drawn.push_back({{x, y + chordHeight, width, 24}, {x, y, width, 20}, piece});
                    x += width;
                    first = false;
                    if (stop == offset)
                        break;
                    offset = stop;
                } while (offset < remaining.length());
            }
            y += rowHeight;
        }
        if (!used.empty()) {
            float x = 15;
            y += 15;
            for (auto &name : used) {
                if (x + 115 > getWidth() - 15 && x > 15) {
                    x = 15;
                    y += 160;
                }
                diagrams.push_back({name, {x, y, 105, 150}});
                x += 120;
            }
            y += 160;
        }
        needed = (int)y + 20;
        rebuilding = false;
        if (getHeight() != needed)
            setSize(std::max(300, getWidth()), needed);
    }

  public:
    std::function<void(int)> insertAt;
    std::function<void(int, int)> selectTag;
    std::function<void(int, int)> deleteTag;
    std::function<void(int, int, int)> moveTag;
    explicit SheetPreview(App &a) : app(a) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus(true);
        setSize(760, 200);
    }
    void setSource(String value) {
        hoverStart = -1;
        source = std::move(value);
        rebuild();
        repaint();
    }
    void resized() override { rebuild(); }
    void paint(juce::Graphics &g) override {
        g.fillAll(juce::Colour(0xfffffdf6));
        for (auto &item : drawn) {
            if (dragStart < 0 && item.token.chord.isNotEmpty() && item.token.sourceStart == hoverStart) {
                g.setColour(app.theme.selected);
                g.fillRoundedRectangle(chordBounds(item).expanded(2, 1), 3);
            }
            g.setColour(juce::Colour(0xffad793c));
            g.setFont(juce::FontOptions("Segoe UI", 16, juce::Font::bold));
            g.drawText(item.token.chord, item.chord, juce::Justification::left);
            g.setColour(juce::Colour(0xff254962));
            g.setFont(juce::FontOptions("Segoe UI", 17, juce::Font::plain));
            g.drawText(item.token.text, item.lyrics, juce::Justification::left);
        }
        for (auto &diagram : diagrams) {
            g.setColour(app.theme.ink);
            g.setFont(juce::FontOptions("Segoe UI", 16, juce::Font::bold));
            g.drawText(diagram.name, diagram.rect.withHeight(22), juce::Justification::centred);
            J chord = {{"name", utf(diagram.name.upToFirstOccurrenceOf("/", false, false))},
                       {"bass", utf(diagram.name.fromFirstOccurrenceOf("/", false, false))}};
            Document visual = app.doc;
            visual.j["transpose"] = 0;
            drawFretboard(g, diagram.rect.withTrimmedTop(22), app.theory.chosen(chord, visual));
        }
        if (dragStart >= 0) {
            if (dropAt >= 0) {
                g.setColour(app.theme.bronze);
                g.fillRect(dropPoint.x, dropPoint.y - 20, 2.f, 44.f);
            }
            juce::Font font(juce::FontOptions("Segoe UI", 16, juce::Font::bold));
            auto badge =
                juce::Rectangle<float>(dragPoint.x + 12, dragPoint.y - 32, textWidth(font, dragName) + 20, 28)
                    .constrainedWithin(getLocalBounds().toFloat());
            g.setColour(app.theme.paper);
            g.fillRoundedRectangle(badge, 5);
            g.setColour(app.theme.bronze);
            g.drawRoundedRectangle(badge, 5, 1);
            g.setFont(font);
            g.drawText(dragName, badge, juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent &e) override {
        grabKeyboardFocus();
        for (auto &item : drawn)
            if (item.token.chord.isNotEmpty() && chordBounds(item).contains(e.position)) {
                const int start = item.token.sourceStart - item.token.chord.length() - 2;
                const int end = item.token.sourceStart;
                if (e.mods.isPopupMenu()) {
                    if (deleteTag)
                        deleteTag(start, end);
                    return;
                }
                if (e.mods.isCtrlDown()) {
                    dragStart = start;
                    dragEnd = end;
                    dragSource = source;
                    dragName = item.token.chord;
                    dragPoint = e.position;
                    grabKeyboardFocus();
                    repaint();
                    return;
                }
                if (selectTag)
                    selectTag(start, end);
                return;
            }
        if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
            return;
        for (auto &item : drawn)
            if (item.lyrics.contains(e.position)) {
                auto font = juce::Font(juce::FontOptions("Segoe UI", 17, juce::Font::plain));
                int index = 0;
                for (int i = 0; i < item.token.text.length(); ++i) {
                    float at = textWidth(font, item.token.text.substring(0, i + 1));
                    if (e.position.x - item.lyrics.getX() < at) {
                        index = i;
                        break;
                    }
                    index = i + 1;
                }
                if (insertAt)
                    insertAt(item.token.sourceStart + index);
                return;
            }
    }
    void mouseMove(const juce::MouseEvent &e) override {
        int next = -1;
        for (const auto &item : drawn)
            if (item.token.chord.isNotEmpty() && chordBounds(item).contains(e.position)) {
                next = item.token.sourceStart;
                break;
            }
        if (hoverStart != next) {
            hoverStart = next;
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent &) override {
        hoverStart = -1;
        repaint();
    }
    void mouseDrag(const juce::MouseEvent &e) override {
        if (dragStart < 0)
            return;
        dragPoint = e.position;
        dropAt = insertionAt(e.position, &dropPoint);
        repaint();
    }
    void mouseUp(const juce::MouseEvent &) override {
        const int start = dragStart, end = dragEnd, target = dropAt;
        const bool valid =
            start >= 0 && target >= 0 && source == dragSource && (target < start || target > end);
        cancelDrag();
        if (valid && moveTag)
            moveTag(start, end, target);
    }
    bool keyPressed(const juce::KeyPress &key) override {
        if (key == juce::KeyPress::escapeKey && dragStart >= 0) {
            cancelDrag();
            return true;
        }
        return false;
    }
};
class SheetEditor : public juce::Component {
    friend void verifySheetEditor(App &, const File &);
    App &app;
    struct Tabs : juce::TabbedComponent {
        std::function<void()> changed;
        Tabs() : juce::TabbedComponent(juce::TabbedButtonBar::TabsAtTop) {}
        void currentTabChanged(int, const String &) override {
            if (changed)
                changed();
        }
    } tabs;
    juce::Viewport viewport;
    SheetPreview preview;
    struct SourceEditor : juce::TextEditor {
        using juce::TextEditor::newTransaction;
    } editor;
    juce::Label hint;
    juce::ComboBox chords;
    juce::TextButton insert{tr("Insérer au curseur")}, rebuild{tr("Recalculer le placement auto")},
        save{tr("Enregistrer")}, cancel{tr("Annuler")};
    CloseableDialog *dialog = nullptr;
    void replaceText(int start, int end, const String &value) {
        editor.newTransaction();
        editor.setHighlightedRegion({start, end});
        editor.insertTextAtCaret(value);
        editor.newTransaction();
        preview.setSource(editor.getText());
    }
    void addChord(int position) {
        auto name = chords.getText().trim();
        if (pitch(name) < 0)
            return;
        position = std::clamp(position, 0, editor.getText().length());
        replaceText(position, position, "[" + name + "]");
    }

  public:
    SheetEditor(App &a) : app(a), preview(a) {
        hint.setText(tr("Accords au-dessus des mots ou syllabes. Cliquer un mot pour insérer l’accord "
                        "choisi. Le texte source conserve les sauts de ligne."),
                     juce::dontSendNotification);
        hint.setFont(juce::FontOptions(14));
        addAndMakeVisible(hint);
        editor.setMultiLine(true, true);
        editor.setReturnKeyStartsNewLine(true);
        editor.setFont(juce::FontOptions("Consolas", 17, juce::Font::plain));
        auto source = text(app.doc.j, "song_sheet");
        if (source.isEmpty())
            source = initialSheet(app.doc);
        editor.setText(source, false);
        viewport.setViewedComponent(&preview, false);
        viewport.setScrollBarsShown(true, false);
        tabs.addTab("Song sheet", app.theme.paper, &viewport, false);
        tabs.addTab(tr("Texte source"), app.theme.paper, &editor, false);
        addAndMakeVisible(tabs);
        chords.setEditableText(true);
        std::set<String> seen;
        int id = 1;
        for (auto &c : app.doc.chords()) {
            auto n = label(c, app.doc);
            if (n != "N" && seen.insert(n).second)
                chords.addItem(n, id++);
        }
        if (id > 1)
            chords.setSelectedId(1);
        else
            chords.setText("C");
        for (auto *component :
             std::initializer_list<juce::Component *>{&chords, &insert, &rebuild, &save, &cancel})
            addAndMakeVisible(component);
        editor.onTextChange = [this] { preview.setSource(editor.getText()); };
        tabs.changed = [this] { resized(); };
        insert.onClick = [this] { addChord(editor.getCaretPosition()); };
        preview.insertAt = [this](int at) { addChord(at); };
        preview.deleteTag = [this](int a, int b) { replaceText(a, b, {}); };
        preview.moveTag = [this](int a, int b, int target) {
            const auto original = editor.getText();
            const auto tag = original.substring(a, b);
            const auto without = original.substring(0, a) + original.substring(b);
            if (target > b)
                target -= b - a;
            replaceText(0, original.length(), without.substring(0, target) + tag + without.substring(target));
        };
        preview.selectTag = [this](int a, int b) {
            tabs.setCurrentTabIndex(1);
            editor.setHighlightedRegion({std::max(0, a), b});
            editor.grabKeyboardFocus();
        };
        rebuild.onClick = [this] {
            if (juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::QuestionIcon, tr("Placement automatique"),
                    tr("Remplacer les placements édités par une nouvelle proposition ?"), "Recalculer",
                    tr("Annuler"), this))
                replaceText(0, editor.getText().length(), initialSheet(app.doc));
        };
        save.onClick = [this] {
            if (dialog)
                dialog->exitModalState(1);
        };
        cancel.onClick = [this] {
            if (dialog)
                dialog->exitModalState(0);
        };
        preview.setSource(source);
        setSize(1000, 760);
    }
    void paint(juce::Graphics &g) override { g.fillAll(app.theme.paper); }
    bool keyPressed(const juce::KeyPress &key) override {
        if (!key.getModifiers().isCtrlDown() && !key.getModifiers().isCommandDown())
            return false;
        const auto letter = juce::CharacterFunctions::toLowerCase((juce::juce_wchar)key.getKeyCode());
        if (letter != 'z' && letter != 'y')
            return false;
        const bool redo = letter == 'y' || key.getModifiers().isShiftDown();
        if (redo)
            editor.redo();
        else
            editor.undo();
        preview.setSource(editor.getText());
        return true;
    }
    String source() const { return editor.getText(); }
    String generatedSource() const { return initialSheet(app.doc); }
    void setDialog(CloseableDialog *d) { dialog = d; }
    void resized() override {
        auto r = getLocalBounds().reduced(18);
        hint.setBounds(r.removeFromTop(45));
        auto footer = r.removeFromBottom(35);
        cancel.setBounds(footer.removeFromRight(110));
        footer.removeFromRight(10);
        save.setBounds(footer.removeFromRight(130));
        r.removeFromBottom(10);
        auto toolbar = r.removeFromBottom(35);
        chords.setBounds(toolbar.removeFromLeft(150));
        toolbar.removeFromLeft(10);
        insert.setVisible(tabs.getCurrentTabIndex() == 1);
        if (insert.isVisible()) {
            insert.setBounds(toolbar.removeFromLeft(160));
            toolbar.removeFromLeft(10);
        }
        rebuild.setBounds(toolbar.removeFromLeft(235));
        r.removeFromBottom(10);
        tabs.setBounds(r);
        preview.setSize(std::max(300, viewport.getMaximumVisibleWidth()), preview.getHeight());
    }
};
void App::editSheet() {
    if (jobActive)
        return;
    auto content = std::make_unique<SheetEditor>(*this);
    auto *editor = content.get();
    auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    auto *dialog = presentDialog(
        tr("Song sheet · paroles et accords"), std::move(content), std::min(1100, area.getWidth() - 40),
        std::min(840, area.getHeight() - 40), [safe = safePointer()](int code, juce::Component &component) {
            if (safe && code == 1) {
                safe->snapshot();
                safe->doc.j["song_sheet"] = utf(static_cast<SheetEditor &>(component).source());
                safe->doc.j["song_sheet_generated"] =
                    utf(static_cast<SheetEditor &>(component).generatedSource());
                safe->setStatus(tr("Song sheet enregistrée dans le projet"));
            }
        });
    editor->setDialog(dialog);
}

void verifySheetEditor(App &app, const File &output) {
    {
        const auto documentBefore = app.doc.j;
        SheetPreview preview(app);
        preview.setLookAndFeel(&app.theme);
        preview.setSize(760, 200);
        J gaps = J::array();
        for (String prefix : {String("First"), String("[C]First")}) {
            preview.setSource(prefix + "\nSecond");
            int baseline = preview.getHeight();
            for (int blank = 1; blank <= 3; ++blank) {
                preview.setSource(prefix + String::repeatedString("\n", blank + 1) + "Second");
                require(preview.getHeight() == baseline + 24 * blank, "Blank song-sheet lines expanded");
                gaps.push_back({{"chord_line", prefix.startsWithChar('[')},
                                {"blank_lines", blank},
                                {"extra_height", preview.getHeight() - baseline}});
            }
        }
        Document sheet = app.doc;
        sheet.audioName = "Line breaks.wav";
        sheet.j["song_sheet"] = "[C]First line\nSecond line\n\n[G]Third line\n\n\nChorus\n[Am]Fourth line";
        exportPdf(sheet, app.theory, output.getChildFile("sheet-line-breaks.pdf"), true);
        preview.setSource(text(sheet.j, "song_sheet"));
        auto image = preview.createComponentSnapshot(preview.getLocalBounds());
        auto stream = output.getChildFile("sheet-line-breaks.png").createOutputStream();
        juce::PNGImageFormat png;
        png.writeImageToStream(image, *stream);
        writeJson(output.getChildFile("sheet-line-breaks.json"),
                  {{"checks", gaps}, {"source", sheet.j["song_sheet"]}});
        require(app.doc.j == documentBefore, "Preview modified the existing document");
        preview.setLookAndFeel(nullptr);
    }
    SheetEditor editor(app);
    editor.setLookAndFeel(&app.theme);
    editor.setSize(1050, 740);
    require(!editor.insert.isVisible(), "Cursor insertion exposed in preview");
    editor.tabs.setCurrentTabIndex(1);
    require(editor.insert.isVisible(), "Source-text cursor insertion missing");
    editor.tabs.setCurrentTabIndex(0);
    const auto documentBefore = app.doc.j;
    const String original = "[Cm]First second [G]third";
    editor.editor.setText(original);
    editor.preview.setSource(original);
    auto eventFor = [&](juce::Point<float> point) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier, 1,
                                0, 0, 0, 0, &editor.preview, &editor.preview, juce::Time::getCurrentTime(),
                                point, juce::Time::getCurrentTime(), 1, false);
    };
    auto chordPoint = editor.preview.chordBounds(editor.preview.drawn.front()).getCentre();
    editor.preview.mouseMove(eventFor(chordPoint));
    require(editor.preview.hoverStart == editor.preview.drawn.front().token.sourceStart &&
                editor.preview.dragName.isEmpty() && editor.editor.getText() == original,
            "Hover did not highlight the existing chord in place");
    {
        auto image = editor.createComponentSnapshot(editor.getLocalBounds());
        auto stream = output.getChildFile("song-editor-hover.png").createOutputStream();
        juce::PNGImageFormat png;
        require(png.writeImageToStream(image, *stream), "Hover snapshot failed");
    }
    editor.preview.mouseExit(eventFor(chordPoint));
    require(editor.preview.hoverStart < 0, "Hover remained after pointer exit");
    auto targetPiece = editor.preview.drawn.back();
    auto targetPoint = targetPiece.lyrics.getCentre();
    const int target = editor.preview.insertionAt(targetPoint);
    editor.preview.mouseDown(eventFor(chordPoint));
    editor.preview.mouseDrag(eventFor(targetPoint));
    require(editor.preview.dragName == "Cm" && editor.preview.dropAt == target,
            "Chord drag feedback missing");
    require(editor.editor.getText() == original, "Chord changed before drop");
    auto dragImage = editor.createComponentSnapshot(editor.getLocalBounds());
    {
        auto stream = output.getChildFile("song-editor-drag.png").createOutputStream();
        juce::PNGImageFormat png;
        require(png.writeImageToStream(dragImage, *stream), "Drag snapshot failed");
    }
    editor.preview.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    editor.preview.mouseUp(eventFor(targetPoint));
    require(editor.editor.getText() == original, "Escape committed a chord drag");
    editor.preview.mouseDown(eventFor(chordPoint));
    editor.preview.mouseDrag(eventFor(targetPoint));
    editor.preview.mouseUp(eventFor(targetPoint));
    const auto without = original.substring(4);
    const auto expected = without.substring(0, target - 4) + "[Cm]" + without.substring(target - 4);
    require(editor.editor.getText() == expected, "Chord drop changed text or another chord");
    const juce::KeyPress undoKey('Z', juce::ModifierKeys::ctrlModifier, 'z'),
        redoKey('Y', juce::ModifierKeys::ctrlModifier, 'y'),
        redoShiftKey('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 'Z');
    editor.keyPressed(undoKey);
    require(editor.editor.getText() == original && editor.preview.source == original,
            "Preview undo did not restore dragged chord");
    editor.keyPressed(redoKey);
    require(editor.editor.getText() == expected, "Preview redo did not restore dragged chord");
    editor.keyPressed(undoKey);
    require(editor.editor.getText() == original, "Second drag undo failed: " + editor.editor.getText());
    editor.keyPressed(redoShiftKey);
    require(editor.editor.getText() == expected, "Ctrl Shift Z redo failed: " + editor.editor.getText());
    auto moved = std::find_if(editor.preview.drawn.begin(), editor.preview.drawn.end(),
                              [](const auto &item) { return item.token.chord == "Cm"; });
    require(moved != editor.preview.drawn.end(), "Moved chord missing");
    const auto point = editor.preview.chordBounds(*moved).getCentre();
    auto rightClick = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                       juce::ModifierKeys::rightButtonModifier, 1, 0, 0, 0, 0,
                                       &editor.preview, &editor.preview, juce::Time::getCurrentTime(), point,
                                       juce::Time::getCurrentTime(), 1, false);
    const int modalsBeforeDelete = juce::ModalComponentManager::getInstance()->getNumModalComponents();
    editor.preview.mouseDown(rightClick);
    require(editor.editor.getText() == without, "Chord deletion changed lyrics or another chord");
    require(juce::ModalComponentManager::getInstance()->getNumModalComponents() == modalsBeforeDelete,
            "Right-click deletion opened a menu or confirmation");
    editor.keyPressed(undoKey);
    require(editor.editor.getText() == expected, "Deletion undo failed");
    editor.keyPressed(undoKey);
    require(editor.editor.getText() == original, "Consecutive edits were grouped into one undo");
    editor.keyPressed(redoKey);
    editor.keyPressed(redoKey);
    require(editor.editor.getText() == without, "Consecutive redo failed");
    editor.chords.setText("Am");
    editor.addChord(0);
    require(editor.editor.getText() == "[Am]" + without, "Preview insertion failed");
    editor.keyPressed(undoKey);
    require(editor.editor.getText() == without, "Insertion undo failed");
    editor.keyPressed(redoKey);
    require(editor.editor.getText() == "[Am]" + without, "Insertion redo failed");
    require(app.doc.j == documentBefore, "Preview editing changed musical analysis before save");
    editor.editor.setText(text(app.doc.j, "song_sheet").isEmpty() ? initialSheet(app.doc)
                                                                  : text(app.doc.j, "song_sheet"));
    auto image = editor.createComponentSnapshot(editor.getLocalBounds());
    auto stream = output.getChildFile("song-editor.png").createOutputStream();
    stream->setPosition(0);
    stream->truncate();
    juce::PNGImageFormat png;
    require(png.writeImageToStream(image, *stream), tr("Image song sheet impossible"));
    editor.setLookAndFeel(nullptr);
}
} // namespace kn
