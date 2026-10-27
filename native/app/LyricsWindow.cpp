#include "UI.h"
namespace kn {
void Timeline::paintLyricsWindow(juce::Graphics &g, juce::Rectangle<float> lane) {
    displayLyricCue = nullptr;
    const int active = activeLyricCue(app.doc.lyrics(), position);
    const bool instrumental = active < 0;
    if (active >= 0)
        displayLyricCue = &app.doc.lyrics()[(size_t)active];
    int slots = lane.getHeight() >= 530 ? 5 : 3;
    const float rowHeight = lane.getHeight() / slots;
    const float center = lane.getCentreY();
    auto drawRow = [&](int index, int slot, bool current) {
        if (index < 0 || index >= (int)app.doc.lyrics().size())
            return;
        const auto &cue = app.doc.lyrics()[(size_t)index];
        juce::Rectangle<float> row(lane.getX(), center + (slot - .5f) * rowHeight, lane.getWidth(),
                                   rowHeight);
        lyricRows.push_back({index, row, current});
        auto phrase =
            row.withY(row.getCentreY() - lyricFontHeight() * .55f).withHeight(lyricFontHeight() * 1.35f);
        auto value = text(cue, "text");
        juce::Font font(juce::FontOptions("Segoe UI", lyricFontHeight(), juce::Font::plain));
        g.setFont(font);
        const float width = lyricWidth(font, value);
        const float origin = row.getX() + std::max(0.f, (row.getWidth() - width) * .5f);
        const int wordIndex = current ? activeLyricWord(cue, position) : -1;
        if (wordIndex >= 0) {
            const auto &word = words(cue)[(size_t)wordIndex];
            int offset = word.at("offset");
            float a = lyricWidth(font, value.substring(0, offset)),
                  b = lyricWidth(font, value.substring(0, offset + text(word, "text").length()));
            g.setColour(app.theme.selected);
            g.fillRoundedRectangle(
                juce::Rectangle<float>(origin + a, phrase.getY(), b - a, phrase.getHeight())
                    .getIntersection(row),
                3);
        }
        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(row.toNearestInt());
            g.setColour(app.theme.ink.withAlpha(current ? 1.f : .48f));
            float x = origin;
            for (int n = 0; n < value.length();) {
                int end = n + 1;
                bool whitespace = juce::CharacterFunctions::isWhitespace(value[n]);
                while (end < value.length() &&
                       juce::CharacterFunctions::isWhitespace(value[end]) == whitespace)
                    ++end;
                auto run = value.substring(n, end);
                float advance = lyricWidth(font, run);
                if (!whitespace)
                    g.drawText(run, juce::Rectangle<float>(x, phrase.getY(), advance + 2, phrase.getHeight()),
                               juce::Justification::centredLeft, false);
                x += advance;
                n = end;
            }
        }
        lyricHits.push_back({phrase, cue.at("start"), value});
        for (auto &word : words(cue)) {
            int at = word.at("offset");
            float a = lyricWidth(font, value.substring(0, at)),
                  b = lyricWidth(font, value.substring(0, at + text(word, "text").length()));
            auto box = juce::Rectangle<float>(origin + a, phrase.getY(), b - a, phrase.getHeight())
                           .getIntersection(row);
            if (!box.isEmpty())
                lyricHits.push_back({box, word.at("start"), text(word, "text")});
        }
        // Existing chord placement is reused with the identical text origin. Its virtual bottom
        // positions the names above this phrase without changing any musical timestamp.
        auto annotationArea = row.withBottom(phrase.getY() + lyricFontHeight() * 1.5f);
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(row.toNearestInt());
        paintLyricChords(g, current && position >= cue.at("end").get<double>() ? nullptr : &cue,
                         annotationArea, origin);
    };
    if (!instrumental) {
        for (int delta = -slots / 2; delta <= slots / 2; ++delta)
            drawRow(active + delta, delta, delta == 0);
    } else {
        // Completed lyrics disappear in instrumental gaps. Keep the real chords visible.
        auto strip =
            juce::Rectangle<float>(lane.getX(), center - rowHeight * .5f, lane.getWidth(), rowHeight);
        paintLyricChords(g, nullptr, strip, strip.getX());
    }
}
} // namespace kn
