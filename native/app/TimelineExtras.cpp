#include "UI.h"
namespace kn {
namespace {
float textWidth(const juce::Font &font, const String &text) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0, 0);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}
} // namespace
std::vector<int> Timeline::chordChanges() const {
    std::vector<int> result;
    String previous;
    double end = -1;
    for (int i = 0; i < (int)app.doc.chords().size(); ++i) {
        const auto &c = app.doc.chords()[(size_t)i];
        auto name = label(c, app.doc);
        double start = c.at("start");
        if (name != previous || start > end + .02)
            result.push_back(i);
        previous = name;
        end = c.at("end");
    }
    return result;
}
double Timeline::readableSpan() const {
    const double duration = std::max(2., app.doc.duration());
    juce::Font font(juce::FontOptions("Segoe UI", 19, juce::Font::bold));
    double required = 0;
    int previous = -1;
    for (int index : chordChanges()) {
        const auto &c = app.doc.chords()[(size_t)index];
        if (text(c, "name") == "N")
            continue;
        if (previous >= 0) {
            const auto &p = app.doc.chords()[(size_t)previous];
            double gap = c.at("start").get<double>() - p.at("start").get<double>();
            if (gap > 0)
                required = std::max(required, (textWidth(font, label(p, app.doc)) + 28.) / gap);
        }
        previous = index;
    }
    return required > 0 ? std::min(duration, std::max(1, getWidth() - 86) / (required * 1.10)) : duration;
}
void Timeline::applyReadableZoom() {
    readableZoomActive = true;
    span = readableSpan();
    offset = std::clamp(position, 0., std::max(0., app.doc.duration() - span));
    app.syncZoom();
    repaint();
}
void Timeline::resized() {
    const bool widthChanged = readableWidth != getWidth();
    readableWidth = getWidth();
    if (readableZoomActive && widthChanged) {
        span = readableSpan();
        follow(position);
        app.syncZoom();
    }
}
double Timeline::tickStep() const {
    const double capacity = std::max(1., (getWidth() - 70.) / 100.);
    const double target = span / capacity;
    double decade = std::pow(10., std::floor(std::log10(std::max(1e-12, target))));
    for (int multiplier : {1, 2, 5, 10})
        if (decade * multiplier >= target)
            return decade * multiplier;
    return decade * 10;
}
J Timeline::viewState() const {
    return {{"span", span},          {"offset", offset},        {"readableZoom", readableZoomActive},
            {"audio", audioVisible}, {"chords", chordsVisible}, {"lyrics", lyricsVisible}};
}
void Timeline::restoreView(const J &state) {
    if (!state.is_object())
        return;
    if (state.contains("audio") && state["audio"].is_boolean())
        audioVisible = state["audio"];
    if (state.contains("chords") && state["chords"].is_boolean())
        chordsVisible = state["chords"];
    if (state.contains("lyrics") && state["lyrics"].is_boolean())
        lyricsVisible = state["lyrics"];
    applyReadableZoom();
}
float Timeline::lyricWidth(const juce::Font &font, const String &value) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, value, 0, 0);
    float width = glyphs.getBoundingBox(0, -1, true).getWidth();
    juce::GlyphArrangement spaces;
    spaces.addLineOfText(font, "a a", 0, 0);
    juce::GlyphArrangement letters;
    letters.addLineOfText(font, "aa", 0, 0);
    float extra =
        (spaces.getBoundingBox(0, -1, true).getWidth() - letters.getBoundingBox(0, -1, true).getWidth()) *
        .5f;
    // Include trailing spaces: glyph bounds omit them, whereas advances preserve them.
    float advance = 0;
    for (int i = 0; i < glyphs.getNumGlyphs(); ++i) {
        auto &glyph = glyphs.getGlyph(i);
        advance = std::max(advance, glyph.getRight());
    }
    for (auto c : value)
        if (juce::CharacterFunctions::isWhitespace(c))
            advance += extra;
    return std::max(width, advance);
}
int Timeline::currentChord() const {
    for (int i = 0; i < (int)app.doc.chords().size(); ++i) {
        auto &c = app.doc.chords()[(size_t)i];
        if (c.at("start").get<double>() <= position && position < c.at("end").get<double>())
            return i;
    }
    // During an unannotated gap anticipate the next event; at the end retain the final event.
    for (int i = 0; i < (int)app.doc.chords().size(); ++i)
        if (app.doc.chords()[(size_t)i].at("start").get<double>() > position)
            return i;
    return (int)app.doc.chords().size() - 1;
}
void Timeline::follow(double seconds) {
    if (drag.isNotEmpty())
        return;
    offset = std::clamp(seconds - span * .28, 0., std::max(0., app.doc.duration() - span));
}
void Timeline::paintChordCards(juce::Graphics &g) {
    auto b = bands();
    if (!g.getClipBounds().intersects(
            juce::Rectangle<int>(55, (int)b[1], getWidth() - 70, (int)(b[2] - b[1]) + 1)))
        return;
    miniBoxes.clear();
    chordCards.clear();
    if (!chordsVisible)
        return;
    float left = 55, width = std::max(1.f, (getWidth() - 70.f - 24) / 5);
    int current = currentChord();
    float height = b[2] - b[1];
    for (int slot = 0; slot < 5; ++slot) {
        int index = current + slot - 2;
        juce::Rectangle<float> box(left + slot * (width + 6), b[1], width, std::max(1.f, height));
        if (index < 0 || index >= (int)app.doc.chords().size())
            continue;
        auto &chord = app.doc.chords()[(size_t)index];
        chordCards.push_back({index, box});
        if (text(chord, "name") == "N")
            continue;
        bool active = slot == 2 && chord.at("start").get<double>() <= position &&
                      position < chord.at("end").get<double>();
        g.setColour(active ? app.theme.selected : juce::Colour(0xffedf1ef));
        g.fillRoundedRectangle(box, 6);
        g.setColour(active ? app.theme.bronze : app.theme.border);
        g.drawRoundedRectangle(box, 6, active ? 1.8f : 1.f);
        g.setColour(app.theme.ink);
        g.setFont(juce::FontOptions("Segoe UI", std::clamp(width * .13f, 20.f, 32.f),
                                    active ? juce::Font::bold : juce::Font::plain));
        g.drawText(label(chord, app.doc), box.reduced(6, 2).removeFromTop(36), juce::Justification::centred,
                   false);
        float h = std::max(0.f, std::min(box.getHeight() - 43, (width - 22) * 180.f / 140.f));
        if (h < 36)
            continue;
        juce::Rectangle<float> diagram(box.getCentreX() - h * 140.f / 360.f, box.getY() + 39,
                                       h * 140.f / 180.f, h);
        try {
            auto shape = app.theory.chosen(chord, app.doc);
            if (!shape.empty()) {
                drawFretboard(g, diagram, shape, false);
                miniBoxes.push_back({index, diagram});
            }
        } catch (const std::exception &) {
        }
    }
}
void Timeline::paintLyricChords(juce::Graphics &g, const J *cue, juce::Rectangle<float> strip, float origin) {
    struct Item {
        int index;
        float x, width;
        bool anchored;
    };
    std::vector<Item> items;
    juce::Font wordFont(juce::FontOptions("Segoe UI", lyricFontHeight(), juce::Font::plain));
    juce::Font chordFont(juce::FontOptions("Segoe UI", 25, juce::Font::bold));
    double from = 0, to = app.doc.duration();
    if (cue) {
        from = cue->at("start");
        to = cue->at("end");
    } else
        for (auto &line : app.doc.lyrics()) {
            if (lyricCueEnd(line) <= position)
                from = std::max(from, lyricCueEnd(line));
            if (line.at("start").get<double>() > position) {
                to = line.at("start");
                break;
            }
        }
    int active = currentChord();
    const bool currentRow = !cue || cue == displayLyricCue;
    auto changes = chordChanges();
    int carried = -1;
    for (int i : changes)
        if (app.doc.chords()[(size_t)i].at("start").get<double>() <= from)
            carried = i;
    for (int i : changes) {
        const auto &c = app.doc.chords()[(size_t)i];
        double start = c.at("start");
        if ((start < from && i != carried) || start >= to || text(c, "name") == "N")
            continue;
        float width = textWidth(chordFont, label(c, app.doc)) + 8, at = 0;
        bool anchored = false;
        if (cue) {
            auto value = text(*cue, "text");
            const auto &ws = words(*cue);
            at = origin - width - 10;
            for (size_t n = 0; n < ws.size(); ++n) {
                const auto &w = ws[n];
                double a = w.at("start"), b = w.at("end");
                int off = w.at("offset");
                float wx = origin + lyricWidth(wordFont, value.substring(0, off));
                float end = origin + lyricWidth(wordFont, value.substring(0, off + text(w, "text").length()));
                if (start < a) {
                    if (n > 0) {
                        const auto &prev = ws[n - 1];
                        float previousEnd =
                            origin +
                            lyricWidth(wordFont, value.substring(0, prev.at("offset").get<int>() +
                                                                        text(prev, "text").length()));
                        at = (previousEnd + wx - width) * .5f;
                    }
                    break;
                }
                if (start < b || (a == b && start == a)) {
                    at = wx;
                    anchored = true;
                    break;
                }
                at = end + 12;
            }
            // Un-timed text: keep annotations above the phrase, using time proportions only for display.
            if (ws.empty())
                at = origin + (float)std::clamp((start - from) / std::max(.001, to - from), 0., 1.) *
                                  lyricWidth(wordFont, value);
        }
        items.push_back({i, at, width, anchored});
    }
    g.setFont(chordFont);
    float total = 0;
    for (auto &item : items)
        total += item.width + 20;
    float running = strip.getX() + std::max(0.f, (strip.getWidth() - total) / 2);
    if (!cue && total > strip.getWidth()) {
        float before = 0;
        for (auto &item : items) {
            if (item.index > active)
                break;
            before += item.width + 20;
        }
        running -= std::clamp(before - strip.getWidth() * .65f, 0.f, total - strip.getWidth());
    }
    std::vector<juce::Rectangle<float>> placed;
    for (auto &item : items) {
        float at =
            cue ? std::clamp(item.x, strip.getX(), std::max(strip.getX(), strip.getRight() - item.width))
                : running;
        float y = cue ? strip.getBottom() - lyricFontHeight() * 1.5f - 34 : strip.getCentreY() - 15;
        juce::Rectangle<float> box(at, y, item.width, 29);
        if (cue) {
            // First use the horizontal room above the word; stack only when the row is genuinely full.
            for (auto &previous : placed)
                if (box.expanded(3, 0).intersects(previous) &&
                    previous.getRight() + 8 + box.getWidth() <= strip.getRight())
                    box.setX(previous.getRight() + 8);
            // Collisions stack upwards only; never place a chord below the lyrics.
            bool collision = true;
            while (collision) {
                collision = false;
                for (auto &prev : placed)
                    if (box.expanded(3, 0).intersects(prev)) {
                        box.translate(0, -31);
                        collision = true;
                        break;
                    }
            }
        }
        placed.push_back(box);
        g.setColour(item.index == active && currentRow ? app.theme.bronze
                                                       : app.theme.ink.withAlpha(currentRow ? 1.f : .48f));
        g.drawText(label(app.doc.chords()[(size_t)item.index], app.doc), box,
                   juce::Justification::centredLeft, false);
        lyricChordLabels.push_back({item.index, box, item.anchored, item.index == active && currentRow});
        running += item.width + 20;
    }
}
} // namespace kn
