#include "UI.h"
namespace kn {
void Timeline::prepareWaveform() {
    waveTiles.clear();
    wavePeaks.clear();
    cacheSource = app.source.get();
    cacheDensity = -1;
    cacheHeight = 0;
    if (!app.source || app.source->mono.empty())
        return;
    ++wavePeakBuilds;
    const auto &samples = app.source->mono;
    std::vector<Peak> base((samples.size() + 255) / 256);
    for (size_t n = 0; n < samples.size(); ++n) {
        auto &p = base[n / 256];
        p.low = std::min(p.low, samples[n]);
        p.high = std::max(p.high, samples[n]);
    }
    wavePeaks.push_back(std::move(base));
    while (wavePeaks.back().size() > 1) {
        const auto &previous = wavePeaks.back();
        std::vector<Peak> next((previous.size() + 1) / 2);
        for (size_t n = 0; n < previous.size(); ++n) {
            next[n / 2].low = std::min(next[n / 2].low, previous[n].low);
            next[n / 2].high = std::max(next[n / 2].high, previous[n].high);
        }
        wavePeaks.push_back(std::move(next));
    }
}
Timeline::Peak Timeline::peakRange(int64_t first, int64_t last) const {
    Peak result;
    const auto &samples = app.source->mono;
    first = std::clamp<int64_t>(first, 0, (int64_t)samples.size());
    last = std::clamp<int64_t>(last, first, (int64_t)samples.size());
    auto scan = [&](int64_t a, int64_t b) {
        for (auto n = a; n < b; ++n) {
            result.low = std::min(result.low, samples[(size_t)n]);
            result.high = std::max(result.high, samples[(size_t)n]);
        }
    };
    int64_t left = (first + 255) / 256, right = last / 256;
    if (left >= right || wavePeaks.empty()) {
        scan(first, last);
        return result;
    }
    scan(first, left * 256);
    scan(right * 256, last);
    size_t level = 0;
    auto include = [&](int64_t n) {
        const auto &p = wavePeaks[level][(size_t)n];
        result.low = std::min(result.low, p.low);
        result.high = std::max(result.high, p.high);
    };
    while (left < right) {
        if (left & 1)
            include(left++);
        if (right & 1)
            include(--right);
        left /= 2;
        right /= 2;
        ++level;
    }
    return result;
}
size_t Timeline::waveformBytes() const {
    size_t bytes = 0;
    for (auto &tile : waveTiles)
        bytes += (size_t)tile.image.getWidth() * tile.image.getHeight() * 4;
    return bytes;
}
void Timeline::paintWaveform(juce::Graphics &g, int bottom) {
    if (cacheSource != app.source.get())
        prepareWaveform();
    const double density = std::max(1, getWidth() - 70) * renderScale / span;
    const int height = std::max(1, (int)std::round((bottom - 35 - 7) * renderScale));
    if (cacheDensity != density || cacheHeight != height) {
        waveTiles.clear();
        cacheDensity = density;
        cacheHeight = height;
    }
    // The raster grid is anchored to absolute audio time, never to the moving viewport.
    const int tileWidth =
        (int)std::max<size_t>(1, std::min<size_t>(256, waveCacheLimit / ((size_t)height * 4)));
    const auto scroll = (int64_t)std::floor(offset * density);
    const auto first = scroll / tileWidth;
    const auto last = (scroll + (int64_t)std::ceil((getWidth() - 70) * renderScale)) / tileWidth;
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(juce::Rectangle<int>(55, 35, getWidth() - 70, std::max(1, bottom - 35)));
    g.addTransform(juce::AffineTransform::scale(1.f / renderScale));
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    for (int64_t index = first; index <= last; ++index) {
        auto found =
            std::find_if(waveTiles.begin(), waveTiles.end(), [&](auto &tile) { return tile.index == index; });
        if (found == waveTiles.end()) {
            const size_t bytes = (size_t)tileWidth * height * 4;
            while (!waveTiles.empty() && waveformBytes() + bytes > waveCacheLimit)
                waveTiles.pop_front();
            juce::Image image(juce::Image::ARGB, tileWidth, height, true, juce::SoftwareImageType());
            juce::Graphics raster(image);
            raster.setColour(app.theme.ink);
            for (int column = 0; column < tileWidth; ++column) {
                const auto global = index * tileWidth + column;
                int64_t a = (int64_t)std::floor(global * 16000. / density);
                int64_t b = std::max(a + 1, (int64_t)std::floor((global + 1) * 16000. / density));
                if (a >= (int64_t)app.source->mono.size())
                    continue;
                auto peak = peakRange(a, b);
                int top = std::clamp((int)std::round(height * .5 - peak.high * height * .46), 0, height - 1);
                int end = std::clamp((int)std::round(height * .5 - peak.low * height * .46), top, height - 1);
                raster.fillRect(column, top, 1, end - top + 1);
            }
            waveTiles.push_back({index, image});
            found = std::prev(waveTiles.end());
            ++waveTileBuilds;
        }
        g.drawImageAt(found->image, (int)(std::round(55 * renderScale) + index * tileWidth - scroll),
                      (int)std::round(35 * renderScale));
    }
}
void Timeline::refreshPlayback(double seconds) {
    const double oldPosition = position, oldOffset = offset;
    const float oldX = x(position);
    const int oldChord = currentChord();
    auto lyricState = [&](double at) {
        const int cue = activeLyricCue(app.doc.lyrics(), at);
        std::pair<int, int> result{cue, cue < 0 ? -1 : activeLyricWord(app.doc.lyrics()[(size_t)cue], at)};
        return result;
    };
    const auto previousLyric = lyricState(position);
    position = seconds;
    follow(seconds);
    playbackRegions.clear();
    if (position == oldPosition && offset == oldOffset)
        return;
    auto b = bands();
    auto dirty = [&](juce::Rectangle<int> area) {
        area = area.getIntersection(getLocalBounds());
        if (!area.isEmpty()) {
            playbackRegions.push_back(area);
            repaint(area);
        }
    };
    if (audioVisible) {
        if (offset != oldOffset)
            dirty({55, 0, getWidth() - 70, (int)std::ceil(b[0])});
        else {
            dirty({(int)std::floor(oldX) - 3, 22, 7, std::max(1, (int)b[0] - 22)});
            dirty({(int)std::floor(x(position)) - 3, 22, 7, std::max(1, (int)b[0] - 22)});
        }
    }
    const bool changedChord = oldChord != currentChord();
    if (chordsVisible && changedChord)
        dirty({55, (int)b[1], getWidth() - 70, (int)std::ceil(b[2] - b[1])});
    if (lyricsVisible) {
        int y = (int)b[2] + 5, height = getHeight() - y - hintHeight();
        if (previousLyric != lyricState(position) || (!audioVisible && !chordsVisible && changedChord))
            dirty({55, y, getWidth() - 70, height});
    }
}
void Timeline::animate() {
    if (!isShowing())
        return;
    double seconds;
    {
        juce::ScopedLock guard(app.audio.lock);
        if (!app.audio.playing)
            return;
        seconds = app.audio.position / 16000. * app.audio.timeFactor;
    }
    refreshPlayback(seconds);
}
} // namespace kn
