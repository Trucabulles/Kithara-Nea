#include "UI.h"
#include <limits>
namespace kn {
static float widthOf(const juce::Font &font, const String &text) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0, 0);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}
Theme::Theme() {
    setColourScheme(getLightColourScheme());
    setColour(juce::ToggleButton::textColourId, ink);
    setColour(juce::ToggleButton::tickColourId, bronze);
    setColour(juce::ComboBox::arrowColourId, ink);
    setColour(juce::ResizableWindow::backgroundColourId, paper);
    setColour(juce::TextButton::buttonColourId, button);
    setColour(juce::TextButton::buttonOnColourId, selected);
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, ink);
    setColour(juce::ComboBox::backgroundColourId, control);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::outlineColourId, border);
    setColour(juce::Label::textColourId, ink);
    setColour(juce::HyperlinkButton::textColourId, bronze);
    setColour(juce::Slider::thumbColourId, bronze);
    setColour(juce::Slider::trackColourId, bronze);
    setColour(juce::Slider::backgroundColourId, juce::Colour(0xff97a5ad));
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxBackgroundColourId, control);
    setColour(juce::Slider::textBoxOutlineColourId, border);
    setColour(juce::PopupMenu::backgroundColourId, paper);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::headerTextColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, hover);
    setColour(juce::PopupMenu::highlightedTextColourId, ink);
    setColour(juce::TextEditor::backgroundColourId, control);
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::outlineColourId, border);
    setColour(juce::TextEditor::focusedOutlineColourId, bronze);
    setColour(juce::TextEditor::highlightColourId, selected);
    setColour(juce::ListBox::backgroundColourId, control);
    setColour(juce::ListBox::outlineColourId, border);
    setColour(juce::AlertWindow::backgroundColourId, paper);
    setColour(juce::AlertWindow::textColourId, ink);
    setColour(juce::AlertWindow::outlineColourId, border);
}
juce::Font Theme::getTextButtonFont(juce::TextButton &, int) { return juce::FontOptions(17); }
juce::Font Theme::getComboBoxFont(juce::ComboBox &) { return juce::FontOptions(17); }
juce::Font Theme::getLabelFont(juce::Label &label) {
    return label.getFont().withHeight(std::max(
        label.getParentComponent() && dynamic_cast<juce::Slider *>(label.getParentComponent()) ? 17.f : 16.f,
        label.getFont().getHeight()));
}
void Theme::drawLabel(juce::Graphics &g, juce::Label &label) {
    if (!label.getProperties().contains("keyValueBold")) {
        juce::LookAndFeel_V4::drawLabel(g, label);
        return;
    }
    const int prefix = (int)label.getProperties()["keyCaptionLength"];
    const auto font = getLabelFont(label).withStyle(juce::Font::plain);
    const auto colour = label.findColour(juce::Label::textColourId);
    juce::AttributedString text;
    text.append(label.getText().substring(0, prefix), font, colour);
    text.append(label.getText().substring(prefix),
                (bool)label.getProperties()["keyValueBold"] ? font.boldened() : font, colour);
    text.setJustification(juce::Justification::centredLeft);
    const auto bounds = label.getBorderSize().subtractedFrom(label.getLocalBounds()).toFloat();
    juce::TextLayout layout;
    layout.createLayout(text, bounds.getWidth());
    layout.draw(g, bounds);
}
void Theme::drawToggleButton(juce::Graphics &g, juce::ToggleButton &button, bool over, bool down) {
    const float tickWidth = 18.7f;
    drawTickBox(g, button, 4.f, (button.getHeight() - tickWidth) * .5f, tickWidth, tickWidth,
                button.getToggleState(), button.isEnabled(), over, down);
    g.setColour(button.findColour(juce::ToggleButton::textColourId)
                    .withMultipliedAlpha(button.isEnabled() ? 1.f : .5f));
    g.setFont(juce::FontOptions(17));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(29).withTrimmedRight(2),
                     juce::Justification::centredLeft, 2, 1.f);
}
juce::Button *Theme::createDocumentWindowButton(int type) {
    auto *button = new juce::TextButton(type == juce::DocumentWindow::closeButton      ? tr("Fermer")
                                        : type == juce::DocumentWindow::maximiseButton ? tr("Agrandir")
                                                                                       : tr("Réduire"));
    button->setComponentID(type == juce::DocumentWindow::closeButton      ? "windowClose"
                           : type == juce::DocumentWindow::maximiseButton ? "windowMax"
                                                                          : "windowMin");
    return button;
}
juce::Component *Theme::getParentComponentForMenuOptions(const juce::PopupMenu::Options &options) {
    if (auto *parent = options.getParentComponent())
        return parent;
    if (auto *target = options.getTargetComponent())
        return target->getTopLevelComponent();
    return nullptr;
}
void Theme::fillResizableWindowBackground(juce::Graphics &g, int w, int h,
                                          const juce::BorderSize<int> &borderSize,
                                          juce::ResizableWindow &window) {
    if (!dynamic_cast<CloseableDialog *>(&window)) {
        juce::LookAndFeel_V4::fillResizableWindowBackground(g, w, h, borderSize, window);
        return;
    }
    g.setColour(paper);
    g.fillRoundedRectangle(juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(.5f), 12);
}
void Theme::drawResizableWindowBorder(juce::Graphics &g, int w, int h,
                                      const juce::BorderSize<int> &borderSize,
                                      juce::ResizableWindow &window) {
    if (!dynamic_cast<CloseableDialog *>(&window)) {
        juce::LookAndFeel_V4::drawResizableWindowBorder(g, w, h, borderSize, window);
        return;
    }
    g.setColour(border);
    g.drawRoundedRectangle(juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(.5f), 12, 1);
}
juce::AlertWindow *Theme::createAlertWindow(const String &title, const String &message, const String &first,
                                            const String &second, const String &third,
                                            juce::MessageBoxIconType icon, int count,
                                            juce::Component *owner) {
    auto *window =
        juce::LookAndFeel_V4::createAlertWindow(title, message, first, second, third, icon, count, owner);
    window->setOpaque(false); // Configure the native surface before it is shown, never during paint.
    roundNativeWindow(*window);
    return window;
}
void Theme::drawAlertBox(juce::Graphics &g, juce::AlertWindow &box, const juce::Rectangle<int> &,
                         juce::TextLayout &text) {
    auto bounds = box.getLocalBounds().toFloat().reduced(1);
    g.setColour(paper);
    g.fillRoundedRectangle(bounds, 12);
    g.setColour(border);
    g.drawRoundedRectangle(bounds, 12, 1);
    g.setColour(ink);
    text.draw(g,
              {24.f, 22.f, bounds.getWidth() - 48, bounds.getHeight() - getAlertWindowButtonHeight() - 48});
}
void Theme::drawDocumentWindowTitleBar(juce::DocumentWindow &window, juce::Graphics &g, int width, int height,
                                       int titleX, int titleWidth, const juce::Image *icon,
                                       bool titleOnLeft) {
    if (dynamic_cast<CloseableDialog *>(&window)) {
        g.fillAll(paper);
        if (!window.getName().startsWith(tr("À propos")) && !window.getName().startsWith("About")) {
            g.setColour(ink);
            g.setFont(juce::FontOptions(17));
            g.drawText(window.getName(), titleX, 0, titleWidth, height, juce::Justification::centredLeft);
        }
        return;
    }
    juce::LookAndFeel_V4::drawDocumentWindowTitleBar(window, g, width, height, titleX, titleWidth, icon,
                                                     titleOnLeft);
}
juce::Typeface::Ptr Theme::getTypefaceForFont(const juce::Font &font) {
    return juce::LookAndFeel_V4::getTypefaceForFont(
        juce::Font(juce::FontOptions("Segoe UI", font.getHeight(), font.getStyleFlags())));
}
void Theme::drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &colour, bool over,
                                 bool down) {
    auto id = b.getComponentID();
    if (id == "help")
        return;
    if (id.startsWith("window")) {
        if (over || down) {
            g.setColour(id == "windowClose" ? juce::Colour(0xffdfb8a8) : hover);
            g.fillAll();
        }
        return;
    }
    auto r = b.getLocalBounds().toFloat().reduced(.5f);
    if (id == "speaker") {
        auto circle = r.withSizeKeepingCentre(std::min(r.getWidth(), r.getHeight()),
                                              std::min(r.getWidth(), r.getHeight()));
        g.setColour(down ? selected : over ? hover : button);
        g.fillEllipse(circle);
        g.setColour(border);
        g.drawEllipse(circle, 1.f);
        return;
    }
    g.setColour(down ? colour.darker(.1f) : over ? colour.brighter(.04f) : colour);
    g.fillRoundedRectangle(r, 5);
    g.setColour(border);
    g.drawRoundedRectangle(r, 5, 1);
}
void Theme::drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area, bool separator,
                              bool active, bool highlighted, bool ticked, bool sub, const String &text,
                              const String &shortcut, const juce::Drawable *icon,
                              const juce::Colour *colour) {
    if (separator) {
        g.setColour(border);
        g.drawHorizontalLine(area.getCentreY(), (float)area.getX() + 8, (float)area.getRight() - 8);
        return;
    }
    if (highlighted) {
        g.setColour(juce::Colour(0xffe9e0d1));
        g.fillRect(area);
    }
    g.setColour((colour ? *colour : ink).withMultipliedAlpha(active ? 1.f : .4f));
    g.setFont(juce::FontOptions(16));
    auto row = area.reduced(9, 0);
    auto check = row.removeFromLeft(18);
    if (ticked) {
        float x = check.getCentreX() - 4.f, y = (float)check.getCentreY();
        juce::Path p;
        p.startNewSubPath(x, y);
        p.lineTo(x + 3, y + 3);
        p.lineTo(x + 8, y - 4);
        g.strokePath(p,
                     juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (icon) {
        auto badge = row.removeFromLeft(22).toFloat().withSizeKeepingCentre(12.f, 12.f);
        juce::Graphics::ScopedSaveState state(g);
        icon->drawWithin(g, badge, juce::RectanglePlacement::centred, active ? 1.f : .4f);
    }
    if (sub) {
        auto a = row.removeFromRight(12);
        g.drawText(juce::String::fromUTF8("›"), a, juce::Justification::centred);
    }
    if (shortcut.isNotEmpty()) {
        auto a = row.removeFromRight(std::min(100, row.getWidth() / 3));
        g.drawText(shortcut, a, juce::Justification::centredRight);
    }
    g.drawText(text, row, juce::Justification::centredLeft);
}

void Theme::drawButtonText(juce::Graphics &g, juce::TextButton &b, bool over, bool down) {
    auto id = b.getComponentID();
    auto c = b.getLocalBounds().toFloat().getCentre();
    g.setColour(ink.withMultipliedAlpha(b.isEnabled() ? 1.f : .38f));
    if (id == "foldInstrument") {
        juce::Path p;
        const float direction = b.getToggleState() ? -1.f : 1.f;
        p.startNewSubPath(c.x - 4, c.y - 2 * direction);
        p.lineTo(c.x, c.y + 2 * direction);
        p.lineTo(c.x + 4, c.y - 2 * direction);
        g.strokePath(p, juce::PathStrokeType(1.5f));
        return;
    }
    if (id == "speaker") {
        static auto icon = [] {
            auto xml = juce::XmlDocument::parse(
                R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M3 9 H7 L12 5 V19 L7 15 H3 Z" fill="none" stroke="#254962" stroke-width="1.6" stroke-linejoin="round"/><path d="M15 8 Q20 12 15 16 M18 5 Q25 12 18 19" fill="none" stroke="#254962" stroke-width="1.6" stroke-linecap="round"/></svg>)");
            return juce::Drawable::createFromSVGString(xml->toString());
        }();
        if (icon)
            icon->drawWithin(g, b.getLocalBounds().toFloat().reduced(6), juce::RectanglePlacement::centred,
                             b.isEnabled() ? 1.f : .38f);
        return;
    }
    if (id == "file") {
        juce::Path p;
        for (float y : {-4.5f, 0.f, 4.5f}) {
            p.startNewSubPath(c.x - 6, c.y + y);
            p.lineTo(c.x + 6, c.y + y);
        }
        g.strokePath(
            p, juce::PathStrokeType(1.125f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }
    if (id == "help") {
        g.drawEllipse(b.getLocalBounds().toFloat().reduced(3), 1.4f);
        g.setFont(juce::FontOptions(19, juce::Font::bold));
        g.drawText("?", b.getLocalBounds(), juce::Justification::centred);
        return;
    }
    if (id.startsWith("window")) {
        if (id == "windowClose") {
            g.drawLine(c.x - 4, c.y - 4, c.x + 4, c.y + 4, 1.5f);
            g.drawLine(c.x + 4, c.y - 4, c.x - 4, c.y + 4, 1.5f);
        } else if (id == "windowMax")
            g.drawRect(c.x - 4, c.y - 4, 8.f, 8.f, 1.5f);
        else
            g.drawLine(c.x - 5, c.y, c.x + 5, c.y, 1.5f);
        return;
    }
    if (id == "record") {
        g.setColour((b.getToggleState() ? juce::Colours::white : juce::Colour(0xffb44735))
                        .withMultipliedAlpha(b.isEnabled() ? 1.f : .4f));
        if (b.getToggleState())
            g.fillRect(c.x - 5, c.y - 5, 10.f, 10.f);
        else
            g.fillEllipse(c.x - 6.5f, c.y - 6.5f, 13, 13);
        return;
    }
    if (id == "begin" || id == "end") {
        juce::Path p;
        if (id == "begin") {
            g.fillRect(c.x - 6, c.y - 6, 1.5f, 12.f);
            p.addTriangle(c.x - 3, c.y, c.x + 5, c.y - 6, c.x + 5, c.y + 6);
        } else {
            g.fillRect(c.x + 4.5f, c.y - 6, 1.5f, 12.f);
            p.addTriangle(c.x + 3, c.y, c.x - 5, c.y - 6, c.x - 5, c.y + 6);
        }
        g.fillPath(p);
        return;
    }
    if (id == "play") {
        if (b.getToggleState()) {
            g.fillRect(c.x - 5, c.y - 6, 3.f, 12.f);
            g.fillRect(c.x + 2, c.y - 6, 3.f, 12.f);
        } else {
            juce::Path p;
            p.addTriangle(c.x - 4, c.y - 6, c.x - 4, c.y + 6, c.x + 6, c.y);
            g.fillPath(p);
        }
        return;
    }
    if (id == "loop") {
        juce::Path p;
        p.startNewSubPath(c.x - 7, c.y + 2);
        p.lineTo(c.x - 7, c.y - 4);
        p.lineTo(c.x + 6, c.y - 4);
        p.startNewSubPath(c.x + 7, c.y - 2);
        p.lineTo(c.x + 7, c.y + 4);
        p.lineTo(c.x - 6, c.y + 4);
        g.strokePath(p, juce::PathStrokeType(1.6f));
        juce::Path arrows;
        arrows.addTriangle(c.x + 3, c.y - 7, c.x + 3, c.y - 1, c.x + 8, c.y - 4);
        arrows.addTriangle(c.x - 3, c.y + 1, c.x - 3, c.y + 7, c.x - 8, c.y + 4);
        g.fillPath(arrows);
        return;
    }
    if (id == "stop") {
        g.fillRect(c.x - 5, c.y - 5, 10.f, 10.f);
        return;
    }
    juce::LookAndFeel_V4::drawButtonText(g, b, over, down);
}
Timeline::Timeline(App &a) : app(a) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    setWantsKeyboardFocus(true);
    animation = std::make_unique<juce::VBlankAttachment>(this, [this](double) { animate(); });
}
float Timeline::x(double value) const {
    const double density = std::max(1, getWidth() - 70) * renderScale / span;
    return (
        float)((std::round(55 * renderScale) + std::round(value * density) - std::floor(offset * density)) /
               renderScale);
}
double Timeline::time(float pixel) const {
    const double density = std::max(1, getWidth() - 70) * renderScale / span;
    return std::max(0., (pixel * renderScale - std::round(55 * renderScale) + std::floor(offset * density)) /
                            density);
}
void Timeline::fit() {
    readableZoomActive = false;
    offset = 0;
    span = std::max(2., app.doc.duration());
    repaint();
}
void Timeline::zoom(double factor, double anchor) {
    readableZoomActive = false;
    if (anchor < 0)
        anchor = position;
    double f = (anchor - offset) / span;
    // Short recordings still need context when zooming out. This is a view limit only.
    span = std::clamp(span * factor, std::min(.001, readableSpan()), std::max(30., app.doc.duration()));
    offset = std::max(0., anchor - f * span);
    app.syncZoom();
    repaint();
}
String Timeline::hintText() const {
    return tr("Molette : zoom · Clic molette : déplacer · Maj + glisser : boucle · Horodatages : Édition des "
              "données");
}
int Timeline::hintTextHeight() const {
    const float width = widthOf(juce::Font(juce::FontOptions(16)), hintText());
    return 6 + 20 * std::max(1, (int)std::ceil(width * 1.12f / std::max(1, getWidth() - 65 - 320)));
}
int Timeline::hintHeight() const {
    // The footer contains both text and 30 px controls, with 4 px clearance on
    // each side. Reserve this inside the existing timeline, not in its minimum.
    return std::max(38, hintTextHeight());
}
std::array<float, 3> Timeline::bands() const {
    bool onlyLyrics = lyricsVisible && !audioVisible && !chordsVisible;
    bool lyrics = lyricsVisible && (!app.doc.lyrics().empty() || (onlyLyrics && !app.doc.chords().empty()));
    float available = std::max(30, getHeight() - 38 - hintHeight()),
          lyricHeight = lyrics ? std::clamp(available * .20f, 64.f, 86.f) : 0,
          remaining = std::max(15.f, available - lyricHeight - (lyrics ? 12 : 0)),
          chord = chordsVisible
                      ? (audioVisible
                             ? std::min(std::max(15.f, remaining - 20), std::max(120.f, remaining * .58f))
                             : remaining - 6)
                      : 0,
          wave = audioVisible ? std::max(0.f, remaining - chord - (chordsVisible ? 6 : 0)) : 0;
    float bottom = 35 + wave, top = bottom + (wave && chord ? 6 : 0);
    return {bottom, top, top + chord};
}
float Timeline::chordFontHeight(float visibleWidth) const {
    auto b = bands();
    return std::clamp(std::min((b[2] - b[1]) * .3f, visibleWidth * .12f), 20.f, 32.f);
}
float Timeline::lyricFontHeight() const {
    if (lyricsVisible && !audioVisible && !chordsVisible)
        return std::clamp((getHeight() - 38.f - hintHeight()) / 6.f, 22.f, 30.f);
    auto b = bands();
    return std::clamp((getHeight() - hintHeight() - 9.f - b[2]) * .42f, 22.f, 30.f);
}
void Timeline::paint(juce::Graphics &g) {
    renderScale = std::max(1.f, g.getInternalContext().getPhysicalPixelScaleFactor());
    g.fillAll(app.theme.control);
    if (app.recordingDeadline > 0) {
        const auto remaining = std::max(1, (int)std::ceil(
            (app.recordingDeadline - juce::Time::getMillisecondCounterHiRes()) / 1000.));
        g.setColour(app.theme.bronze);
        g.setFont(juce::FontOptions(64));
        g.drawText(String(remaining), getLocalBounds().reduced(20), juce::Justification::centred);
        g.setColour(app.theme.ink);
        g.setFont(juce::FontOptions(16));
        g.drawText(tr("Espace ou Échap pour annuler"), 0, getHeight() - 36, getWidth(), 26,
                   juce::Justification::centred);
        return;
    }
    {
        juce::ScopedLock lock(app.audio.lock);
        if (app.audio.writer) {
            auto area = getLocalBounds().reduced(24).toFloat();
            g.setColour(app.theme.ink);
            g.setFont(juce::FontOptions(20));
            g.drawText(tr("Enregistrement en cours"), area.removeFromTop(34), juce::Justification::centred);
            auto centre = area.getCentreY();
            g.setColour(app.theme.ink.withAlpha(.25f));
            g.drawHorizontalLine((int)centre, area.getX(), area.getRight());
            g.setColour(app.audio.recordingClipped ? juce::Colour(0xffb44838) : app.theme.bronze);
            const auto &peaks = app.audio.recordingPeaks;
            for (size_t i = 0; i < peaks.size(); ++i) {
                const auto value = peaks[(app.audio.recordingPeakIndex + i) % peaks.size()];
                const float height = std::min(1.f, std::sqrt(value)) * area.getHeight() * .38f;
                const float x = area.getX() + (float)i / peaks.size() * area.getWidth();
                g.drawLine(x, centre - height, x, centre + height, 2.f);
            }
            g.setColour(app.theme.ink);
            g.setFont(juce::FontOptions(16));
            g.drawText(tr("Niveau d’entrée") + ": " +
                           (app.audio.inputPeak > 0 ? String(20 * std::log10(app.audio.inputPeak), 1) + " dBFS"
                                                   : String("— dBFS")),
                       area.removeFromBottom(28), juce::Justification::centred);
            return;
        }
    }
    auto [waveBottom, chordTop, chordBottom] = bands();
    int left = 55, right = getWidth() - 15;
    g.setFont(juce::FontOptions(16));
    const double step = tickStep();
    {
        juce::Graphics::ScopedSaveState tickClip(g);
        g.reduceClipRegion(left, 0, right - left, getHeight());
        const double textMargin = 75. * span / std::max(1, right - left);
        for (double t = std::max(0., std::floor((offset - textMargin) / step) * step);
             t <= offset + span + textMargin; t += step) {
            auto px = x(t);
            g.setColour(juce::Colour(0xffdce2e4));
            g.drawVerticalLine((int)px, 25.f, chordsVisible ? chordTop : (float)getHeight() - hintHeight());
            if (chordsVisible)
                g.drawVerticalLine((int)px, chordBottom + 5, (float)getHeight() - hintHeight());
            g.setColour(app.theme.ink);
            g.drawText(String::formatted("%d:%04.1f", (int)t / 60, std::fmod(t, 60.)), (int)px - 25, 3, 75,
                       20, juce::Justification::left);
        }
    }
    g.setColour(juce::Colour(0xff738591));
    if (audioVisible)
        g.drawText("Audio", 2, 35, 50, (int)waveBottom - 35, juce::Justification::centredLeft);
    if (chordsVisible)
        g.drawText(tr("Accords"), 2, (int)chordTop, 50, (int)(chordBottom - chordTop),
                   juce::Justification::centredLeft);
    if (audioVisible && app.source &&
        g.getClipBounds().intersects(juce::Rectangle<int>(left, 35, right - left, (int)waveBottom - 35)))
        paintWaveform(g, (int)waveBottom);
    waveChordLabels.clear();
    waveChordMarkers.clear();
    if (audioVisible && app.source && waveBottom > 55) {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(juce::Rectangle<int>(left, 35, right - left, (int)waveBottom - 35));
        g.setFont(juce::FontOptions("Segoe UI", 19, juce::Font::bold));
        String previous;
        double previousEnd = -1;
        float lastLabelRight = std::numeric_limits<float>::lowest();
        std::vector<std::pair<String, juce::Rectangle<float>>> labels;
        String pinnedName, groupName;
        double pinnedStart = -1, groupStart = -1, groupEnd = -1;
        for (const auto &chord : app.doc.chords()) {
            const double start = chord.at("start"), end = chord.at("end");
            if (start > offset)
                break;
            const auto name = label(chord, app.doc);
            if (name != groupName || start > groupEnd + .02)
                groupStart = start;
            groupName = name;
            groupEnd = end;
            if (offset < end && text(chord, "name") != "N") {
                pinnedName = name;
                pinnedStart = groupStart;
            } else {
                pinnedName.clear();
                pinnedStart = -1;
            }
        }
        for (auto &chord : app.doc.chords()) {
            const auto name = label(chord, app.doc);
            const double start = chord.at("start"), end = chord.at("end");
            bool change = name != previous || start > previousEnd + .02;
            previous = name;
            previousEnd = end;
            if (!change)
                continue;
            const float at = x(start);
            if (at >= left && at <= right)
                waveChordMarkers.push_back(start);
            g.setColour(app.theme.bronze.withAlpha(.65f));
            g.drawLine(at, 67, at, std::min(waveBottom - 2, 77.f), 1.5f);
            if (text(chord, "name") == "N")
                continue;
            const float width = widthOf(g.getCurrentFont(), name) + 12;
            juce::Rectangle<float> box(at + 4, 38, width, 26);
            if (box.getX() < lastLabelRight + 8)
                continue; // Keep every marker, but never overlap or move names in time.
            // Resolve collisions against the same absolute sequence, including
            // offscreen names, so scrolling cannot change which labels survive.
            lastLabelRight = box.getRight();
            if (pinnedStart >= 0 && pinnedStart < offset && start <= pinnedStart)
                continue; // Replace this label with its continuation at the left edge.
            if (box.getRight() <= left || box.getX() >= right)
                continue;
            labels.push_back({name, box});
            waveChordLabels.push_back({start, box});
        }
        if (pinnedStart >= 0 && pinnedStart < offset && pinnedName.isNotEmpty()) {
            const float width = widthOf(g.getCurrentFont(), pinnedName) + 12;
            float at = (float)left + 4;
            // The next real label pushes the continuation out progressively.
            // No new change marker or timestamp is introduced.
            if (!labels.empty())
                at = std::min(at, labels.front().second.getX() - width - 8);
            juce::Rectangle<float> box(at, 38, width, 26);
            if (box.getRight() > left) {
                labels.insert(labels.begin(), {pinnedName, box});
                waveChordLabels.insert(waveChordLabels.begin(), {pinnedStart, box});
            }
        }
        // Draw text last so neighbouring dense markers cannot cross the glyphs.
        for (auto &item : labels) {
            g.setColour(app.theme.paper);
            g.fillRoundedRectangle(item.second, 3);
            g.setColour(app.theme.ink);
            g.drawText(item.first, item.second.reduced(6, 0), juce::Justification::centredLeft);
        }
    }
    paintChordCards(g);

    displayedNextChord.clear();
    nextChordBounds = {};
    const bool lyricBand = lyricsVisible && (!app.doc.lyrics().empty() ||
                                             (!audioVisible && !chordsVisible && !app.doc.chords().empty()));
    const bool onlyLyrics = !audioVisible && !chordsVisible && lyricsVisible;
    bool paintLyrics = g.getClipBounds().intersects(
        juce::Rectangle<int>(55, (int)chordBottom + 5, right - left,
                             std::max(1, getHeight() - (int)chordBottom - hintHeight() - 4)));
    if (paintLyrics) {
        lyricHits.clear();
        lyricChordLabels.clear();
        lyricRows.clear();
    }
    if (lyricBand && paintLyrics) {
        float top = chordBottom + 6, bottom = (float)getHeight() - hintHeight() - 3;
        juce::Rectangle<float> lane((float)left, top, (float)(right - left), std::max(1.f, bottom - top));
        g.setColour(juce::Colour(0xfff4f1e8));
        g.fillRect(lane);
        g.setColour(juce::Colour(0xff738591));
        g.setFont(juce::FontOptions(12));
        g.drawText(tr("Paroles"), 2, (int)top + 1, 50, 20, juce::Justification::left);
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(lane.toNearestInt());
        if (onlyLyrics) {
            paintLyricsWindow(g, lane.reduced(8, 4));
        } else {
            g.setFont(juce::FontOptions("Segoe UI", lyricFontHeight(), juce::Font::plain));
            const J *current = nullptr;
            const int currentIndex = activeLyricCue(app.doc.lyrics(), position);
            if (currentIndex >= 0)
                current = &app.doc.lyrics()[(size_t)currentIndex];
            auto strip = lane.reduced(8, 4);
            auto phraseStrip =
                onlyLyrics
                    ? strip.withTop(std::max(strip.getY(), strip.getBottom() - lyricFontHeight() * 1.7f))
                    : strip;
            float textOrigin = strip.getX();
            if (current) {
                auto value = text(*current, "text");
                const J *currentWord = nullptr;
                const int wordIndex = activeLyricWord(*current, position);
                if (wordIndex >= 0)
                    currentWord = &words(*current)[(size_t)wordIndex];
                const float textWidth = lyricWidth(g.getCurrentFont(), value);
                // One origin for the whole phrase, independent of the active word and cursor.
                const float shift = -std::max(0.f, (strip.getWidth() - textWidth) * .5f);
                textOrigin = strip.getX() - shift;
                if (currentWord) {
                    int at = currentWord->at("offset");
                    float start = lyricWidth(g.getCurrentFont(), value.substring(0, at));
                    float end = lyricWidth(g.getCurrentFont(),
                                           value.substring(0, at + text(*currentWord, "text").length()));
                    auto highlight =
                        juce::Rectangle<float>(strip.getX() + start - shift,
                                               phraseStrip.getCentreY() - lyricFontHeight() * .65f,
                                               end - start, lyricFontHeight() * 1.3f)
                            .getIntersection(strip);
                    g.setColour(app.theme.selected);
                    g.fillRoundedRectangle(highlight, 3);
                }
                juce::Graphics::ScopedSaveState clipped(g);
                g.reduceClipRegion(strip.toNearestInt());
                g.setColour(app.theme.ink);
                float runX = textOrigin;
                for (int n = 0; n < value.length();) {
                    int end = n + 1;
                    bool space = juce::CharacterFunctions::isWhitespace(value[n]);
                    while (end < value.length() &&
                           juce::CharacterFunctions::isWhitespace(value[end]) == space)
                        ++end;
                    auto run = value.substring(n, end);
                    float w = lyricWidth(g.getCurrentFont(), run);
                    if (!space)
                        g.drawText(
                            run,
                            juce::Rectangle<float>(runX, phraseStrip.getY(), w + 2, phraseStrip.getHeight()),
                            juce::Justification::centredLeft, false);
                    runX += w;
                    n = end;
                }
                // Invisible word targets preserve seek-on-click with natural text spacing.
                lyricHits.push_back({strip, current->at("start"), value});
                for (auto &word : words(*current)) {
                    int at = word.at("offset");
                    float start = lyricWidth(g.getCurrentFont(), value.substring(0, at));
                    float end =
                        lyricWidth(g.getCurrentFont(), value.substring(0, at + text(word, "text").length()));
                    auto hit = juce::Rectangle<float>(strip.getX() + start - shift, phraseStrip.getY(),
                                                      end - start, phraseStrip.getHeight())
                                   .getIntersection(strip);
                    if (!hit.isEmpty())
                        lyricHits.push_back({hit, word.at("start"), text(word, "text")});
                }
            }
        }
    }
    for (auto &marker : app.doc.j.at("markers")) {
        auto at = x(marker.value("time", 0.));
        if (at >= left && at <= right) {
            g.setColour(app.theme.bronze);
            g.drawText(text(marker, "name"), (int)at + 3, 20, 180, 16, juce::Justification::left);
        }
    }
    std::optional<std::pair<double, double>> activeLoop;
    {
        juce::ScopedLock guard(app.audio.lock);
        activeLoop = app.audio.loop;
    }
    if (activeLoop) {
        g.setColour(app.theme.bronze.withAlpha(.12f));
        g.fillRect(juce::Rectangle<float>(x(activeLoop->first), 25,
                                          x(activeLoop->second) - x(activeLoop->first),
                                          std::max(0.f, waveBottom - 25)));
    }
    auto cursor = x(position);
    if (audioVisible && cursor >= left && cursor <= right) {
        g.setColour(juce::Colour(0xffa94d3f));
        const float waveCentre =
            35.f + std::max(1.f, std::round((waveBottom - 42.f) * renderScale)) / renderScale * .5f;
        g.drawLine(cursor, 22, cursor, waveCentre, 2);
    }
    g.setColour(juce::Colour(0xff738591));
    g.setFont(juce::FontOptions(16));
    g.drawFittedText(hintText(), 55, getHeight() - hintHeight() + (hintHeight() - hintTextHeight()) / 2 + 3,
                     getWidth() - 65 - 320, hintTextHeight() - 6, juce::Justification::centredLeft,
                     (hintTextHeight() - 6) / 20, 1.f);
}
void Timeline::mouseDown(const juce::MouseEvent &e) {
    grabKeyboardFocus();
    for (auto &card : chordCards) {
        if (!card.second.contains(e.position))
            continue;
        if (e.getNumberOfClicks() < 2)
            pressedCard = card.first;
        int index = e.getNumberOfClicks() >= 2 ? pressedCard : card.first;
        if (index < 0 || index >= (int)app.doc.chords().size())
            return;
        app.select(index, false);
        if (e.mods.isRightButtonDown())
            app.chordPopup(index);
        else if (e.getNumberOfClicks() < 2)
            app.seek(app.doc.chords()[(size_t)index].at("start"));
        return;
    }
    double t = time(e.position.x);
    if (e.mods.isMiddleButtonDown()) {
        drag = "pan";
        dragAt = e.position.x;
        dragOffset = offset;
        return;
    }
    if (e.mods.isShiftDown()) {
        drag = "loop";
        dragAt = t;
        {
            juce::ScopedLock guard(app.audio.lock);
            app.audio.loop.reset();
        }
        return;
    }
    if (e.mods.isRightButtonDown()) {
        for (size_t i = 0; i < app.doc.chords().size(); ++i)
            if (app.doc.chords()[i].at("start").get<double>() <= t &&
                t < app.doc.chords()[i].at("end").get<double>()) {
                app.select((int)i);
                app.chordPopup((int)i);
                break;
            }
        return;
    }
    for (auto &annotation : lyricChordLabels)
        if (annotation.bounds.contains(e.position)) {
            app.select(annotation.index, false);
            app.seek(app.doc.chords()[(size_t)annotation.index].at("start"));
            return;
        }
    if (lyricsVisible)
        for (auto i = lyricHits.rbegin(); i != lyricHits.rend(); ++i)
            if (std::get<0>(*i).contains(e.position)) {
                app.seek(std::get<1>(*i));
                return;
            }
    auto b = bands();
    if (chordsVisible && e.position.y >= b[1] && e.position.y < b[2])
        return;
    app.seek(t);
}
void Timeline::mouseDrag(const juce::MouseEvent &e) {
    if (drag == "pan")
        offset = std::max(0., dragOffset - (e.position.x - dragAt) / std::max(1, getWidth() - 70) * span);
    else if (drag == "loop") {
        auto t = time(e.position.x);
        {
            juce::ScopedLock guard(app.audio.lock);
            if (std::abs(t - dragAt) > .05)
                app.audio.loop = {{std::min(dragAt, t), std::max(dragAt, t)}};
        }
    }
    repaint();
}
void Timeline::mouseUp(const juce::MouseEvent &e) {
    if (drag == "edge")
        app.moveBoundary(dragIndex, time(e.position.x));
    {
        juce::ScopedLock guard(app.audio.lock);
        if (app.audio.loop && app.audio.loop->second - app.audio.loop->first < .05)
            app.audio.loop.reset();
    }
    drag.clear();
}
void Timeline::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel) {
    zoom(wheel.deltaY > 0 ? .8 : 1.25, time(e.position.x));
}
void Timeline::mouseDoubleClick(const juce::MouseEvent &e) {
    if (e.position.x < 55) {
        offset = 0;
        app.seek(0);
        repaint();
        return;
    }
    auto bandsNow = bands();
    if (chordsVisible && e.position.y >= bandsNow[1] && e.position.y < bandsNow[2] && pressedCard >= 0) {
        app.select(pressedCard, false);
        app.editChord();
    }
}
EventsTable::EventsTable(App &a, bool isLyrics) : app(a), lyrics(isLyrics) {
    addAndMakeVisible(table);
    table.setModel(this);
    table.setRowHeight(27);
    table.setMultipleSelectionEnabled(false);
    auto &header = table.getHeader();
    header.addColumn(tr("Début"), 1, 90);
    header.addColumn(tr("Fin"), 2, 90);
    header.addColumn(lyrics ? tr("Paroles") : tr("Accord"), 3, lyrics ? 600 : 130);
    if (!lyrics) {
        header.addColumn(tr("Basse"), 4, 90);
        header.addColumn(tr("Notes de l’accord"), 5, 350);
    } else {
        header.addColumn(tr("Mots"), 4, 100);
    }
    table.setColour(juce::ListBox::outlineColourId, app.theme.border);
    table.setOutlineThickness(1);
}
namespace {
struct EditableDataCell : juce::Label {
    std::function<void(const juce::MouseEvent &)> clicked;
    void mouseDown(const juce::MouseEvent &event) override {
        juce::Label::mouseDown(event);
        if (clicked)
            clicked(event);
    }
};
} // namespace
juce::Component *EventsTable::refreshComponentForCell(int row, int column, bool, juce::Component *existing) {
    if ((!lyrics && column == 5) || row < 0 || row >= getNumRows()) {
        delete existing;
        return nullptr;
    }
    if (lyrics && column == 4) {
        auto *button = dynamic_cast<juce::TextButton *>(existing);
        if (!button) {
            delete existing;
            button = new juce::TextButton();
        }
        button->setButtonText(tr("Modifier…"));
        const bool hasWords = !app.doc.lyrics()[(size_t)row].value("words", J::array()).empty();
        button->setEnabled(hasWords);
        button->setTooltip(hasWords ? tr("Modifier les mots horodatés")
                                    : tr("Aucun mot horodaté dans cette ligne."));
        button->onClick = [safe = app.safePointer(), row] {
            if (safe)
                safe->editWords(row);
        };
        return button;
    }
    auto *label = dynamic_cast<EditableDataCell *>(existing);
    if (!label) {
        delete existing;
        label = new EditableDataCell();
    }
    label->clicked = [this, row, column](const juce::MouseEvent &event) {
        if (event.getNumberOfClicks() == 1) {
            table.selectRowsBasedOnModifierKeys(row, event.mods, false);
            cellClicked(row, column, event);
        }
    };
    auto &event = (lyrics ? app.doc.lyrics() : app.doc.chords())[(size_t)row];
    String value = column == 1   ? String(event.at("start").get<double>(), 3)
                   : column == 2 ? String(event.at("end").get<double>(), 3)
                   : column == 3 ? (lyrics ? text(event, "text")
                                           : transpose(text(event, "name"), app.doc.j.value("transpose", 0)))
                                 : transpose(text(event, "bass"), app.doc.j.value("transpose", 0));
    label->onTextChange = nullptr;
    label->setText(value, juce::dontSendNotification);
    label->setFont(juce::FontOptions(16));
    label->setEditable(false, true, false);
    label->setTooltip(tr("Double-cliquer pour modifier. Entrée : valider ; Échap : annuler."));
    if (lyrics && column == 3)
        label->setTooltip(tr("Modifier le texte de la ligne retire ses temps de mots. Pour conserver les "
                             "temps, utiliser Mots."));
    label->onTextChange = [safe = app.safePointer(), weak = juce::Component::SafePointer<juce::Label>(label),
                           row, column, isLyrics = lyrics] {
        const String input = weak ? weak->getText() : String();
        juce::MessageManager::callAsync([safe, row, column, isLyrics, input] {
            if (!safe)
                return;
            String error;
            if (!safe->applyDataCell(row, isLyrics, column, input, error)) {
                safe->setStatus(error);
                safe->chordTable.update();
                safe->lyricTable.update();
            }
        });
    };
    return label;
}
int EventsTable::getNumRows() { return (int)(lyrics ? app.doc.lyrics().size() : app.doc.chords().size()); }
void EventsTable::paintRowBackground(juce::Graphics &g, int row, int width, int height, bool selected) {
    g.fillAll(selected ? app.theme.selected : row % 2 ? app.theme.paper : app.theme.control);
    g.setColour(app.theme.border.withAlpha(.4f));
    g.drawHorizontalLine(height - 1, 0, (float)width);
}
void EventsTable::paintCell(juce::Graphics &g, int row, int column, int width, int height, bool) {
    auto &events = lyrics ? app.doc.lyrics() : app.doc.chords();
    if (row < 0 || row >= (int)events.size())
        return;
    auto &c = events[(size_t)row];
    String value;
    if (column == 1)
        value = String(c.at("start").get<double>(), 2) + " s";
    else if (column == 2)
        value = String(c.at("end").get<double>(), 2) + " s";
    else if (column == 3)
        value = lyrics                   ? text(c, "text")
                : text(c, "name") == "N" ? tr("Sans accord")
                                         : transpose(text(c, "name"), app.doc.j.value("transpose", 0));
    else if (column == 4)
        value = transpose(text(c, "bass"), app.doc.j.value("transpose", 0));
    else {
        juce::StringArray names;
        for (int n : c.value("notes", std::vector<int>()))
            names.add(midiName(n + app.doc.j.value("transpose", 0)));
        value = names.joinIntoString(" ");
    }
    g.setColour(app.theme.ink);
    g.setFont(juce::FontOptions(14));
    g.drawText(value, 6, 0, width - 10, height, juce::Justification::centredLeft);
}
void EventsTable::selectedRowsChanged(int row) {
    if (!selecting && !lyrics)
        app.select(row, false);
}
void EventsTable::cellClicked(int row, int, const juce::MouseEvent &e) {
    if (lyrics && row >= 0 && row < (int)app.doc.lyrics().size())
        app.seek(app.doc.lyrics()[row].at("start"));
    else if (e.mods.isRightButtonDown())
        app.chordPopup(row);
    else if (row >= 0 && row < (int)app.doc.chords().size())
        app.seek(app.doc.chords()[row].at("start"));
}
void EventsTable::cellDoubleClicked(int row, int column, const juce::MouseEvent &) {
    if (lyrics && column == 4)
        app.editWords(row);
}
void EventsTable::resized() {
    table.setBounds(getLocalBounds());
    int width = std::max(250, getWidth() - (lyrics ? 280 : 400));
    table.getHeader().setColumnWidth(lyrics ? 3 : 5, width);
}
void EventsTable::update() {
    table.updateContent();
    table.repaint();
}
void EventsTable::select(int row, bool scroll) {
    selecting = true;
    auto y = table.getViewport()->getViewPosition();
    table.selectRow(row, false, true);
    if (scroll)
        table.scrollToEnsureRowIsOnscreen(row);
    else
        table.getViewport()->setViewPosition(y);
    selecting = false;
}
InstrumentView::InstrumentView(App &a) : app(a) {
    for (auto *c :
         std::initializer_list<juce::Component *>{&family, &familyCaption, &audition, &stringsAudition})
        addAndMakeVisible(c);
    juce::StringArray types = {tr("Guitare"), "Piano",         tr("Ukulélé"),
                               tr("Basse"),   tr("Mandoline"), tr("Banjo")};
    for (int i = 0; i < types.size(); ++i)
        if (i != 1) // The piano is always visible; this selects a string diagram.
            family.addItem(types[i], i + 1);
    familyCaption.setText(tr("Visualiser l’accord pour :"), juce::dontSendNotification);
    familyCaption.setFont(juce::FontOptions("Segoe UI", 17, juce::Font::plain));
    family.onChange = [this] {
        if (!app.filling)
            app.changeFamily();
    };
    for (auto *button : {&audition, &stringsAudition}) {
        button->setComponentID("speaker");
        button->setEnabled(false);
    }
    audition.setTooltip(tr("Écouter au piano les notes affichées"));
    audition.setTitle(tr("Écouter au piano les notes affichées"));
    stringsAudition.setTooltip(tr("Écouter le doigté affiché avec cet instrument"));
    stringsAudition.setTitle(tr("Écouter le doigté affiché avec cet instrument"));
    audition.onClick = [this] { app.audition(); };
    stringsAudition.onClick = [this] { app.auditionStrings(); };
}
void InstrumentView::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(64); // Preserve existing keyboard and diagram geometry.
    auto area = r.toFloat().reduced(8, 2);
    diagramRect = area.removeFromRight(std::min(420.f, area.getWidth() * .30f));
    auto top = juce::Rectangle<int>((int)diagramRect.getX(), 10, (int)diagramRect.getWidth(), 32);
    familyCaption.setBorderSize(juce::BorderSize<int>(0));
    familyCaption.setJustificationType(juce::Justification::centredLeft);
    const int captionWidth = (int)std::ceil(widthOf(familyCaption.getFont(), familyCaption.getText())) + 2;
    if (top.getWidth() < captionWidth + 112) {
        familyCaption.setBounds((int)diagramRect.getX(), 0, (int)diagramRect.getWidth(), 28);
        family.setBounds((int)diagramRect.getX(), 30, (int)diagramRect.getWidth(), 32);
    } else {
        familyCaption.setBounds(top.removeFromLeft(captionWidth));
        top.removeFromLeft(12);
        family.setBounds(top.removeFromLeft(std::min(190, top.getWidth())));
    }
    area.removeFromRight(16);
    const float previewHeight = std::max(1.f, area.getHeight() - 44.f);
    const float scale = std::max(.01f, std::min(area.getWidth() / pianoReference.getWidth(),
                                                previewHeight / pianoReference.getHeight()));
    const float x = area.getX(),
                y = area.getY() + std::min(24.f, std::max(0.f, previewHeight - 162 * scale) * .5f);
    pianoTransform = juce::AffineTransform::scale(scale).translated(x, y);
    pianoRect = pianoReference.transformedBy(pianoTransform);
    audition.setBounds((int)pianoRect.getX(), (int)pianoRect.getBottom() + 12, 32, 32);
    stringsAudition.setBounds((int)diagramRect.getX() + 8, (int)diagramRect.getBottom() - 40, 32, 32);
}
juce::Rectangle<float> InstrumentView::chordNameBounds() const {
    const float centre = (42.f + pianoRect.getY()) * .5f;
    const float height = std::min(28.f, std::max(12.f, pianoRect.getY() - 44.f));
    return {pianoRect.getX(), centre - height * .5f, pianoRect.getWidth(), height};
}
juce::Rectangle<float> InstrumentView::detailBounds() const {
    auto area = diagramRect.reduced(12, 8);
    area.setTop(positionCaptionBounds().getBottom() + 6);
    if (area.isEmpty())
        return {};
    const auto content = app.shapes.empty() ? juce::Rectangle<float>(140, 180)
                                            : fretboardContentBounds(app.shapes[(size_t)app.shapeIndex]);
    float scale =
        std::max(0.f, std::min(area.getWidth() / content.getWidth(), area.getHeight() / content.getHeight()));
    auto fit = [&] {
        return juce::Rectangle<float>(content.getWidth() * scale, content.getHeight() * scale)
            .withCentre(area.getCentre());
    };
    const auto speaker = stringsAudition.getBounds().toFloat().expanded(6);
    if (fit().intersects(speaker)) {
        // Keep the diagram centred, but avoid only the occupied corner. Either
        // its left edge clears the speaker or its bottom edge clears its top.
        const float clearSide = 2 * (area.getCentreX() - speaker.getRight()) / content.getWidth();
        const float clearAbove = 2 * (speaker.getY() - area.getCentreY()) / content.getHeight();
        scale = std::max(0.f, std::min(scale, std::max(clearSide, clearAbove)));
    }
    return fit();
}
juce::Rectangle<float> InstrumentView::positionCaptionBounds() const {
    return diagramRect.reduced(6, 0).withTrimmedTop(4).withHeight(22);
}
String InstrumentView::positionCountText() const {
    auto count = (int)app.shapes.size();
    return String(count) + (count == 1 ? tr(" position disponible — cliquer pour changer")
                                       : tr(" positions disponibles — cliquer pour changer"));
}
String InstrumentView::getTooltip() {
    return diagramRect.contains(getMouseXYRelative().toFloat()) && !app.shapes.empty() ? positionCountText()
                                                                                       : String();
}
void InstrumentView::paint(juce::Graphics &g) {
    g.fillAll(app.theme.paper);
    String name = app.selected >= 0 && app.selected < (int)app.doc.chords().size()
                      ? label(app.doc.chords()[app.selected], app.doc)
                      : juce::String::fromUTF8("—");
    if (name == "N")
        name = tr("Sans accord");
    g.setColour(app.theme.ink);
    g.setFont(juce::FontOptions("Segoe UI", 20, juce::Font::bold));
    g.drawText(name, chordNameBounds(), juce::Justification::centredLeft);
    PianoVoicing voicing;
    if (app.selected >= 0 && app.selected < (int)app.doc.chords().size())
        voicing = pianoVoicing(app.doc.chords()[app.selected], app.doc, app.theory);
    auto notes = voicing.notes();
    if (!diagramRect.isEmpty()) {
        g.setColour(app.theme.control);
        g.fillRect(diagramRect);
        g.setColour(app.theme.border);
        g.drawRect(diagramRect.reduced(.5f), 1.f);
        g.setColour(app.theme.ink);
        g.setFont(juce::FontOptions("Segoe UI", 14, juce::Font::plain));
        g.drawText(positionCountText(), positionCaptionBounds(), juce::Justification::centred);
        auto area = diagramRect.reduced(10, 15);
        if (!app.shapes.empty()) {
            auto rect = detailBounds();
            drawFretboard(g, rect, app.shapes[(size_t)app.shapeIndex], false, true);
        } else
            g.drawText(tr("Choisir un accord"), area, juce::Justification::centred);
    }
    pianoKeys.clear();
    if (!pianoRect.isEmpty()) {
        auto r = pianoReference;
        if (pressedKey >= 0)
            notes.push_back(pressedKey);
        {
            juce::Graphics::ScopedSaveState state(g);
            g.addTransform(pianoTransform);
            drawPiano(g, r, notes, voicing.left, pressedKey, false);
        }
        auto white = [](int n) {
            return n % 12 != 1 && n % 12 != 3 && n % 12 != 6 && n % 12 != 8 && n % 12 != 10;
        };
        std::vector<int> keys;
        for (int n = 36; n <= 84; ++n)
            if (white(n))
                keys.push_back(n);
        float w = r.getWidth() / keys.size(), h = std::min(140.f, r.getHeight() - 22);
        for (size_t i = 0; i < keys.size(); ++i)
            pianoKeys.push_back(
                {keys[i],
                 juce::Rectangle<float>(r.getX() + i * w, r.getY(), w - 1, h).transformedBy(pianoTransform)});
        // Note labels remain screen-legible while the key geometry scales uniformly.
        g.setFont(juce::FontOptions(12));
        g.setColour(app.theme.ink);
        for (const auto &key : pianoKeys)
            if (key.first % 12 == 0)
                g.drawText(midiName(key.first),
                           key.second.withTop(std::max(key.second.getY(), key.second.getBottom() - 18)),
                           juce::Justification::centredLeft, false);
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] < 84 && !white(keys[i] + 1))
                pianoKeys.push_back({keys[i] + 1, juce::Rectangle<float>(r.getX() + (i + 1) * w - w * .31f,
                                                                         r.getY(), w * .62f, h * .64f)
                                                      .transformedBy(pianoTransform)});
    }
}
void InstrumentView::pressKeyAt(juce::Point<float> point) {
    int key = -1;
    for (auto i = pianoKeys.rbegin(); i != pianoKeys.rend(); ++i)
        if (i->second.contains(point)) {
            key = i->first;
            break;
        }
    if (key == pressedKey)
        return;
    pressedKey = key;
    repaint();
    if (key >= 0)
        app.auditionNotes({key});
}
void InstrumentView::mouseDown(const juce::MouseEvent &e) {
    if (pianoRect.contains(e.position)) {
        pressKeyAt(e.position);
        return;
    }
    if (diagramRect.contains(e.position))
        app.chooseShape();
}
void InstrumentView::mouseDrag(const juce::MouseEvent &e) { pressKeyAt(e.position); }
void InstrumentView::mouseUp(const juce::MouseEvent &) {
    pressedKey = -1;
    repaint();
}
void SettingsPanel::caption(String value, juce::Component &control) {
    auto label = std::make_unique<juce::Label>();
    label->setText(value, juce::dontSendNotification);
    label->setFont(juce::FontOptions(16));
    addAndMakeVisible(*label);
    labels.push_back(std::move(label));
    addAndMakeVisible(control);
}
SettingsPanel::SettingsPanel(App &a) : app(a) {
    addAndMakeVisible(beats);
    beats.setToggleState(true, juce::dontSendNotification);
    auto numeric = [&](juce::Slider &slider, double low, double high, double step) {
        slider.setRange(low, high, step);
        slider.setColour(juce::Slider::textBoxTextColourId, a.theme.ink);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, a.theme.control);
        slider.setColour(juce::Slider::textBoxOutlineColourId, a.theme.border);
        slider.setSliderStyle(juce::Slider::IncDecButtons);
        slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 106, 32);
        slider.onValueChange = [this] {
            if (!app.filling)
                app.changeArrangement();
        };
    };
    numeric(transpose, -12, 12, 1);
    numeric(capo, 0, 12, 1);
    numeric(bpm, 0, 400, .1);
    numeric(meter, 2, 12, 1);
    numeric(speed, .5, 2, .1);
    speed.onValueChange = {};
    speed.setValue(1, juce::dontSendNotification);
    caption("Transposition", transpose);
    caption(tr("Capodastre"), capo);
    caption("Tempo BPM", bpm);
    caption(tr("Mesure"), signature);
    caption(tr("Accordage"), tuning);
    caption("Vitesse", speed);
    caption(tr("Qualité"), quality);
    caption(tr("Langue"), language);
    caption(tr("Calcul"), compute);
    compute.addItem(tr("Automatique"), 1);
    compute.addItem("CPU", 2);
    compute.addItem("GPU", 3);
    compute.setSelectedId(1, juce::dontSendNotification);
    compute.setTooltip(tr("GPU compatible en priorité, sinon CPU. Le premier calcul GPU peut être plus long."));
    quality.addItem(tr("Rapide"), 1);
    quality.addItem(tr("Approfondie"), 2);
    quality.setSelectedId(1, juce::dontSendNotification);
    quality.onChange = [this] { app.selectWhisperQuality(); };
    language.addItem("Auto", 1);
    language.setSelectedId(1, juce::dontSendNotification);
    for (auto *button : {&adapt, &transcribeButton, &alignButton, &reportButton})
        addAndMakeVisible(*button);
    adapt.onClick = [this] { app.applyPlayback(); };
    transcribeButton.onClick = [this] { app.transcribeLyrics(); };
    alignButton.onClick = [this] { app.alignLyrics(); };
    reportButton.onClick = [this] { app.alignmentReport(); };
    tuning.onChange = [this] {
        if (app.filling || tuning.getSelectedId() < 1 ||
            tuning.getSelectedId() >= (int)app.theory.tunings.size() + 1)
            return;
        app.snapshot();
        app.doc.j["guitar_tuning"] = app.theory.tunings[(size_t)tuning.getSelectedId() - 1].at("tuning");
        app.select(app.selected, false);
    };
    speed.setVisible(false);
    adapt.setVisible(false);
    language.setVisible(false);
    labels[5]->setVisible(false);
    labels[7]->setVisible(false);
    for (auto *label : {&keyFrom, &keyTo, &rhythmInfo}) {
        addAndMakeVisible(*label);
        label->setFont(juce::FontOptions(16));
    }
    const juce::StringArray signatures = {
        tr("Non déterminée"), "2/4", "3/4", "4/4", "5/4", "6/4", "7/4", "6/8", "7/8", "9/8", "12/8"};
    for (int i = 0; i < signatures.size(); ++i)
        signature.addItem(signatures[i], i + 1);
    signature.setTooltip(
        tr("Indication rythmique pour la song sheet. Corrigeable ; ne déplace pas les événements."));
    signature.onChange = [this] {
        if (app.filling)
            return;
        app.snapshot();
        auto value = signature.getText();
        app.doc.j["meter"] =
            signature.getSelectedId() == 1 ? 0 : value.upToFirstOccurrenceOf("/", false, false).getIntValue();
        app.doc.j["meter_denominator"] =
            signature.getSelectedId() == 1 ? 4 : value.fromFirstOccurrenceOf("/", false, false).getIntValue();
        app.doc.j["meter_source"] = "manual";
        app.refresh();
    };
    bpm.onValueChange = [this] {
        if (app.filling)
            return;
        app.snapshot();
        app.doc.j["bpm"] = bpm.getValue();
        app.doc.j["bpm_source"] = "manual";
        app.refresh();
    };
    setSize(300, 870);
}
void SettingsPanel::refresh() {
    transpose.setTextValueSuffix(tr(" demi-tons"));
    transpose.setValue(app.doc.j.value("transpose", 0), juce::dontSendNotification);
    capo.setValue(app.doc.j.value("capo", 0), juce::dontSendNotification);
    bpm.setValue(app.doc.j.value("bpm", 0.), juce::dontSendNotification);
    int numerator = app.doc.j.value("meter", 0), denominator = app.doc.j.value("meter_denominator", 4);
    String measure = String(numerator) + "/" + String(denominator);
    int measureId = 1;
    for (int i = 0; i < signature.getNumItems(); ++i)
        if (signature.getItemText(i) == measure)
            measureId = signature.getItemId(i);
    signature.setSelectedId(measureId, juce::dontSendNotification);
    auto key = text(app.doc.j, "key");
    auto shown = [](String input) {
        if (input.isEmpty() || pitch(input) < 0)
            return tr("Non déterminée");
        static juce::StringArray names = {"Do",
                                          juce::String::fromUTF8("Ré♭"),
                                          juce::String::fromUTF8("Ré"),
                                          juce::String::fromUTF8("Mi♭"),
                                          "Mi",
                                          "Fa",
                                          juce::String::fromUTF8("Fa♯"),
                                          "Sol",
                                          juce::String::fromUTF8("La♭"),
                                          "La",
                                          juce::String::fromUTF8("Si♭"),
                                          "Si"};
        if (isEnglish())
            return input.replace(" mineur", " minor").replace(" majeur", " major");
        auto space = input.indexOfChar(' ');
        return names[pitch(input)] + (space >= 0 ? input.substring(space) : String());
    };
    const bool transposed = app.doc.j.value("transpose", 0) != 0;
    const auto detectedCaption = tr("Tonalité détectée : ");
    const auto transposedCaption = tr("Après transposition : ");
    keyFrom.getProperties().set("keyValueBold", !transposed);
    keyFrom.getProperties().set("keyCaptionLength", detectedCaption.length());
    keyTo.getProperties().set("keyValueBold", transposed);
    keyTo.getProperties().set("keyCaptionLength", transposedCaption.length());
    keyFrom.setText(detectedCaption + shown(key), juce::dontSendNotification);
    keyTo.setText(transposedCaption +
                      shown(key.isEmpty() ? key : kn::transpose(key, app.doc.j.value("transpose", 0))),
                  juce::dontSendNotification);
    keyTo.setVisible(transposed);
    keyFrom.repaint();
    keyTo.repaint();
    String rhythm = tr("Mesure : ") + (numerator ? measure : tr("non déterminée"));
    auto origin = text(app.doc.j, "meter_source");
    rhythm += origin == "estimated" ? tr(" · proposée, à vérifier")
              : origin == "manual"  ? tr(" · corrigée manuellement")
                                    : tr(" · indication");
    rhythmInfo.setText(rhythm, juce::dontSendNotification);
    auto family = text(app.doc.j, "instrument_family"), current = text(app.doc.j, "guitar_tuning");
    if (family == "piano")
        family = "guitar"; // Legacy projects: the piano is now always displayed alongside strings.
    tuning.clear(juce::dontSendNotification);
    int found = 0;
    for (size_t i = 0; i < app.theory.tunings.size(); ++i) {
        auto &row = app.theory.tunings[i];
        if (text(row, "instrument") == family) {
            tuning.addItem(text(row, "name"), (int)i + 1);
            if (text(row, "tuning") == current)
                found = (int)i + 1;
        }
    }
    if (!found && family != "piano") {
        tuning.addItem(tr("Accordage du projet"), 10000);
        found = 10000;
    }
    tuning.setSelectedId(found, juce::dontSendNotification);
    tuning.setEnabled(family != "piano");
    alignButton.setButtonText(text(app.doc.j, "reference_text").isEmpty()
                                  ? tr("Saisir / aligner les paroles…")
                                  : tr("Aligner les paroles…"));
    reportButton.setEnabled(!app.doc.j.at("reference_matches").empty());
}
void SettingsPanel::resized() {
    int width = getWidth() - 24;
    beats.setBounds(12, 35, width, 30);
    std::array<std::pair<juce::Component *, int>, 6> rows = {
        {{&transpose, 148}, {&capo, 258}, {&bpm, 438}, {&signature, 480}, {&tuning, 348}, {&quality, 642}}};
    std::array<int, 6> indices = {0, 1, 2, 3, 4, 6};
    for (size_t i = 0; i < rows.size(); ++i) {
        labels[indices[i]]->setBounds(12, rows[i].second, 118, 32);
        rows[i].first->setBounds(132, rows[i].second, std::max(100, width - 120), 32);
    }
    labels[0]->setText("Transposition", juce::dontSendNotification);
    transpose.setTextValueSuffix(tr(" demi-tons"));
    keyFrom.setBounds(12, 190, width, 26);
    keyTo.setBounds(12, 218, width, 26);
    rhythmInfo.setBounds(12, 522, width, 44);
    labels[8]->setBounds(12, 684, 118, 32);
    compute.setBounds(132, 684, std::max(100, width - 120), 32);
    transcribeButton.setBounds(12, 732, width, 36);
    alignButton.setBounds(12, 844, width, 36);
    reportButton.setBounds(12, 892, width, 36);
}
void SettingsPanel::paint(juce::Graphics &g) {
    g.fillAll(app.theme.paper);
    g.setColour(app.theme.border);
    g.drawHorizontalLine(582, 12.f, (float)getWidth() - 12);
    g.setColour(app.theme.ink);
    g.setFont(juce::FontOptions("Segoe UI", 20, juce::Font::bold));
    g.drawText(tr("Analyse musicale"), 12, 4, getWidth() - 24, 27, juce::Justification::left);
    g.drawText(tr("Paroles"), 12, 602, getWidth() - 24, 28, juce::Justification::left);
    g.setFont(juce::FontOptions("Segoe UI", 18, juce::Font::bold));
    g.drawText("Arrangement", 12, 112, getWidth() - 24, 26, juce::Justification::left);
    g.drawText("Positions", 12, 314, getWidth() - 24, 26, juce::Justification::left);
    g.drawText(tr("Indications rythmiques"), 12, 404, getWidth() - 24, 26, juce::Justification::left);
    g.setFont(juce::FontOptions(16));
    g.drawFittedText(tr("Analyse locale des accords et de leurs renversements."), 12, 68, getWidth() - 24, 36,
                     juce::Justification::left, 2, 1.f);
    g.drawFittedText(tr("Traitement local et hors ligne. Vérifier le calage sur le chant."), 12, 780,
                     getWidth() - 24, 44, juce::Justification::left, 2, 1.f);
}
WaitCard::WaitCard(App &a) : app(a) {
    setWantsKeyboardFocus(true);
    setVisible(false);
}
void WaitCard::begin(String value) {
    title = value;
    started = juce::Time::getMillisecondCounterHiRes();
    dots = 0;
    setVisible(true);
    toFront(true);
    enterModalState(true, nullptr, false);
    startTimer(350);
    repaint();
}
void WaitCard::end() {
    stopTimer();
    exitModalState(0);
    setVisible(false);
}
void WaitCard::timerCallback() {
    dots = (dots + 1) % 3;
    repaint();
}
bool WaitCard::keyPressed(const juce::KeyPress &key) {
    if (key == juce::KeyPress::escapeKey) {
        app.cancel();
        return true;
    }
    return false;
}
juce::Rectangle<int> WaitCard::titleBounds() const {
    auto value = app.cancelFlag.load() ? String(tr("Annulation en cours")) : title;
    float width = widthOf(juce::Font(juce::FontOptions(24)), value);
    return {(getWidth() - (int)std::ceil(width) - 32) / 2, 278, (int)std::ceil(width) + 1, 34};
}
void WaitCard::paint(juce::Graphics &g) {
    g.setColour(app.theme.paper);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 22.4f);
    g.setColour(app.theme.border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 22.4f, 1);
    if (app.logo.isValid())
        g.drawImageWithin(app.logo, 49, 28, 252, 238, juce::RectanglePlacement::centred);
    g.setColour(app.theme.ink);
    g.setFont(juce::FontOptions(24));
    auto value = app.cancelFlag.load() ? String(tr("Annulation en cours")) : title;
    g.drawText(value, titleBounds(), juce::Justification::centredLeft);
    auto textBounds = titleBounds();
    g.drawText(String::repeatedString(".", dots + 1), textBounds.getRight(), textBounds.getY(), 32,
               textBounds.getHeight(), juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(14));
    auto elapsed = String((int)((juce::Time::getMillisecondCounterHiRes() - started) / 1000)) + " s";
    if (app.jobProgress.load() >= 0)
        elapsed = String((int)(app.jobProgress.load() * 100)) + " % · " + elapsed;
    g.drawText(elapsed, 20, 318, 310, 22, juce::Justification::centred);
}
SplitHandle::SplitHandle(App &a) : app(a) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible(fold);
    fold.setComponentID("foldInstrument");
    fold.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    fold.onClick = [this] { app.toggleInstruments(); };
}
void SplitHandle::resized() { fold.setBounds(std::max(0, getWidth() - 26), 1, 22, 18); }
void SplitHandle::paint(juce::Graphics &g) {
    g.fillAll(juce::Colour(0xffd4c7b2));
    g.setColour(juce::Colour(0xff7e929d));
    g.fillRect(0, 0, getWidth(), 1);
    g.fillRoundedRectangle(getWidth() / 2.f - 24, getHeight() / 2.f - 2, 48, 4, 2);
}
void SplitHandle::mouseDown(const juce::MouseEvent &) { start = app.timeline.getHeight(); }
void SplitHandle::mouseDrag(const juce::MouseEvent &e) {
    if (app.instrumentsHidden)
        return;
    app.timelineManual = true;
    app.timelinePixels = std::max(150, start + e.getDistanceFromDragStartY());
    app.preferredInstrumentHeight =
        std::max(0, app.getHeight() - 242 - 42 - getHeight() - app.timelinePixels);
    app.resized();
}
} // namespace kn
