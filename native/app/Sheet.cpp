#include "Core.h"
#include "Pdf.h"
#include <numeric>
namespace kn {
namespace {
struct WordRef {
    int offset = 0;
    String word, norm;
};
String normal(String input) {
    auto wide = std::wstring(input.toWideCharPointer());
    int size = NormalizeString(NormalizationKD, wide.data(), (int)wide.size(), nullptr, 0);
    if (size > 0) {
        std::wstring out(size, L'\0');
        int written = NormalizeString(NormalizationKD, wide.data(), (int)wide.size(), out.data(), size);
        if (written > 0)
            input = String(out.data(), (size_t)written);
    }
    String result;
    for (auto c : input.toLowerCase())
        if (juce::CharacterFunctions::isLetterOrDigit(c) && !(c >= 0x300 && c <= 0x36f))
            result += String::charToString(c);
    return result;
}
std::vector<WordRef> words(String text, bool skipHeadings) {
    std::vector<WordRef> result;
    for (int pos = 0; pos < text.length();) {
        if (skipHeadings && (pos == 0 || text[pos - 1] == '\n')) {
            int end = text.indexOfChar(pos, '\n');
            if (end < 0)
                end = text.length();
            auto line = text.substring(pos, end).trim();
            if (line.startsWithChar('[') && line.endsWithChar(']')) {
                pos = end + 1;
                continue;
            }
        }
        if (!juce::CharacterFunctions::isLetterOrDigit(text[pos])) {
            ++pos;
            continue;
        }
        int start = pos++;
        while (pos < text.length()) {
            auto c = text[pos];
            if (juce::CharacterFunctions::isLetterOrDigit(c)) {
                ++pos;
                continue;
            }
            if ((c == '\'' || c == 0x2019 || c == '-') && pos + 1 < text.length() &&
                juce::CharacterFunctions::isLetterOrDigit(text[pos + 1])) {
                pos += 2;
                continue;
            }
            break;
        }
        auto word = text.substring(start, pos);
        result.push_back({start, word, normal(word)});
    }
    return result;
}
struct Match {
    int a = 0, b = 0, size = 0;
};
std::vector<Match> matching(const std::vector<String> &a, const std::vector<String> &b, Cancel cancel = {}) {
    std::map<String, std::vector<int>> indices;
    for (int j = 0; j < (int)b.size(); ++j)
        indices[b[j]].push_back(j);
    std::vector<std::array<int, 4>> work = {{{0, (int)a.size(), 0, (int)b.size()}}};
    std::vector<Match> result;
    while (!work.empty()) {
        auto range = work.back();
        work.pop_back();
        auto [alo, ahi, blo, bhi] = range;
        Match best{alo, blo, 0};
        std::map<int, int> previous;
        for (int i = alo; i < ahi; ++i) {
            checkCancel(cancel);
            std::map<int, int> next;
            for (int j : indices[a[i]]) {
                if (j < blo)
                    continue;
                if (j >= bhi)
                    break;
                int n = 1;
                auto found = previous.find(j - 1);
                if (found != previous.end())
                    n += found->second;
                next[j] = n;
                if (n > best.size)
                    best = {i - n + 1, j - n + 1, n};
            }
            previous = std::move(next);
        }
        if (best.size) {
            result.push_back(best);
            if (alo < best.a && blo < best.b)
                work.push_back({alo, best.a, blo, best.b});
            if (best.a + best.size < ahi && best.b + best.size < bhi)
                work.push_back({best.a + best.size, ahi, best.b + best.size, bhi});
        }
    }
    std::sort(result.begin(), result.end(),
              [](auto &x, auto &y) { return std::tie(x.a, x.b) < std::tie(y.a, y.b); });
    std::vector<Match> merged;
    for (auto m : result) {
        if (!merged.empty() && merged.back().a + merged.back().size == m.a &&
            merged.back().b + merged.back().size == m.b)
            merged.back().size += m.size;
        else
            merged.push_back(m);
    }
    merged.push_back({(int)a.size(), (int)b.size(), 0});
    return merged;
}
double similarity(String a, String b) {
    if (a.isEmpty() && b.isEmpty())
        return 1;
    std::vector<String> x, y;
    for (auto c : a)
        x.push_back(String::charToString(c));
    for (auto c : b)
        y.push_back(String::charToString(c));
    int sum = 0;
    for (auto m : matching(x, y))
        sum += m.size;
    return 2. * sum / std::max<size_t>(1, x.size() + y.size());
}
String insertAnchors(String source, const std::map<int, std::vector<String>> &anchors) {
    for (auto i = anchors.rbegin(); i != anchors.rend(); ++i) {
        String tags;
        for (auto &n : i->second)
            tags += "[" + n + "]";
        source = source.substring(0, i->first) + tags + source.substring(i->first);
    }
    return source;
}
} // namespace
std::pair<J, J> align(String reference, const J &recognized, Cancel cancel) {
    auto refs = words(reference, true);
    require(refs.size() <= 20000, tr("Texte trop long : 20 000 mots maximum"));
    std::vector<J> timed;
    std::vector<String> a, b;
    double previous = -1;
    for (auto &cue : recognized)
        for (auto &word : cue.value("words", J::array())) {
            auto n = normal(text(word, "text"));
            double t = word.at("start");
            if (n.isNotEmpty() && t >= previous) {
                timed.push_back(word);
                b.push_back(n);
                previous = t;
            }
        }
    for (auto &r : refs)
        a.push_back(r.norm);
    std::map<int, J> mapped;
    auto keep = [&](int i, int j, String kind, int count = 1) {
        mapped[i] = {{"offset", refs[i].offset},
                     {"text", utf(refs[i].word)},
                     {"start", timed[j].at("start")},
                     {"end", std::max(timed[j].at("start").get<double>(),
                                      timed[j + count - 1].at("end").get<double>())},
                     {"kind", utf(kind)}};
    };
    auto gap = [&](int ai, int ae, int bi, int be) {
        int n = ae - ai, m = be - bi;
        if (!n || !m || (int64_t)n * m > 250000)
            return;
        std::vector<double> costs((n + 1) * (m + 1));
        std::vector<uint8_t> moves(costs.size());
        auto at = [&](int i, int j) { return i * (m + 1) + j; };
        for (int i = 1; i <= n; ++i) {
            costs[at(i, 0)] = i;
            moves[at(i, 0)] = 1;
        }
        for (int j = 1; j <= m; ++j) {
            costs[at(0, j)] = j;
            moves[at(0, j)] = 2;
        }
        for (int i = 1; i <= n; ++i) {
            checkCancel(cancel);
            auto x = a[ai + i - 1];
            for (int j = 1; j <= m; ++j) {
                auto y = b[bi + j - 1];
                double cost = costs[at(i - 1, j)] + 1;
                uint8_t move = 1;
                auto choose = [&](double v, int k) {
                    if (v < cost) {
                        cost = v;
                        move = (uint8_t)k;
                    }
                };
                choose(costs[at(i, j - 1)] + 1, 2);
                if (x == y || (std::min(x.length(), y.length()) >= 4 && similarity(x, y) >= .8))
                    choose(costs[at(i - 1, j - 1)] + (x == y ? 0 : .5), 3);
                if (j >= 2 && x == b[bi + j - 2] + y)
                    choose(costs[at(i - 1, j - 2)] + .15, 4);
                costs[at(i, j)] = cost;
                moves[at(i, j)] = move;
            }
        }
        int i = n, j = m;
        while (i || j) {
            auto move = moves[at(i, j)];
            if (move == 3) {
                keep(ai + i - 1, bi + j - 1, a[ai + i - 1] == b[bi + j - 1] ? "exact" : "near");
                --i;
                --j;
            } else if (move == 4) {
                keep(ai + i - 1, bi + j - 2, "grouped", 2);
                --i;
                j -= 2;
            } else if (move == 1)
                --i;
            else
                --j;
        }
    };
    int pa = 0, pb = 0;
    for (auto block : matching(a, b, cancel)) {
        gap(pa, block.a, pb, block.b);
        for (int i = 0; i < block.size; ++i)
            keep(block.a + i, block.b + i, "exact");
        pa = block.a + block.size;
        pb = block.b + block.size;
    }
    J matches = J::array(), cues = J::array();
    for (auto &m : mapped)
        matches.push_back(m.second);
    auto lines = juce::StringArray::fromLines(reference);
    int base = 0;
    for (auto &line : lines) {
        J aligned = J::array();
        for (auto &m : matches) {
            int offset = m.at("offset");
            if (base <= offset && offset < base + line.length())
                aligned.push_back({{"start", m.at("start")},
                                   {"end", m.at("end")},
                                   {"text", m.at("text")},
                                   {"offset", offset - base}});
        }
        if (!aligned.empty()) {
            double start = aligned.front().at("start"), end = start;
            for (auto &w : aligned)
                end = std::max(end, w.at("end").get<double>());
            if (end == start)
                for (auto &c : recognized)
                    if (c.at("start").get<double>() <= start && start < c.at("end").get<double>()) {
                        end = c.at("end");
                        break;
                    }
            if (end > start)
                cues.push_back({{"start", start}, {"end", end}, {"text", utf(line)}, {"words", aligned}});
        }
        base += line.length() + 1;
    }
    return {matches, cues};
}
static String songTitle(const Document &d) {
    auto name = d.audioName.isNotEmpty() ? d.audioName : d.audio().getFileName();
    static const juce::StringArray audioExtensions = {"wav",  "mp3", "m4a", "flac", "ogg",
                                                      "aiff", "aif", "aac", "opus", "wma"};
    auto extension = name.fromLastOccurrenceOf(".", false, false).toLowerCase();
    return audioExtensions.contains(extension) ? name.upToLastOccurrenceOf(".", false, false) : name;
}
String initialSheet(const Document &d) {
    auto source = text(d.j, "reference_text");
    std::vector<J> events;
    for (auto &c : d.chords()) {
        if (!events.empty() && label(events.back(), d) == label(c, d) &&
            c.at("start").get<double>() <= events.back().at("end").get<double>() + .02)
            events.back()["end"] = std::max(events.back().at("end").get<double>(), c.at("end").get<double>());
        else
            events.push_back(c);
    }
    if (source.isNotEmpty()) {
        std::map<int, std::vector<String>> anchors;
        std::map<int, juce::StringArray> instrumentalLines;
        auto matches = d.j.at("reference_matches");
        for (auto &c : events) {
            if (text(c, "name") == "N")
                continue;
            const J *word = nullptr;
            double time = c.at("start");
            for (auto &w : matches)
                if (w.at("start").get<double>() <= time &&
                    (time < w.at("end").get<double>() ||
                     w.at("start") == w.at("end") && time == w.at("start").get<double>())) {
                    word = &w;
                    break;
                }
            if (!word) {
                const J *prev = nullptr;
                const J *next = nullptr;
                for (auto &w : matches) {
                    if (w.at("end").get<double>() <= time)
                        prev = &w;
                    if (!next && w.at("start").get<double>() >= time)
                        next = &w;
                }
                if (prev && next) {
                    int a = prev->at("offset"), b = next->at("offset");
                    if (!source.substring(a, b).containsChar('\n') &&
                        words(source.substring(a + text(*prev, "text").length(), b), false).empty())
                        word = next;
                }
            }
            auto n = label(c, d);
            if (word) {
                auto &list = anchors[word->at("offset")];
                list.push_back(n);
            } else {
                const J *next = nullptr;
                for (auto &w : matches)
                    if (w.at("start").get<double>() > time) {
                        next = &w;
                        break;
                    }
                int offset = source.length();
                if (next) {
                    int wordOffset = next->at("offset");
                    offset = source.substring(0, wordOffset).lastIndexOfChar('\n') + 1;
                }
                instrumentalLines[offset].add("[" + n + "]");
            }
        }
        // A new lyric line is not a harmonic event: never repeat the held chord.
        // Instrumental groups are inserted at their chronological boundary, never collected in an appendix.
        std::map<int, String> insertions;
        for (auto &[offset, names] : anchors)
            for (auto &name : names)
                insertions[offset] += "[" + name + "]";
        for (auto &[offset, names] : instrumentalLines) {
            String group = names.joinIntoString(" ");
            const bool atEnd = offset == source.length();
            String before = atEnd && !source.endsWithChar('\n') ? "\n" : String();
            insertions[offset] = before + group + (atEnd ? String() : "\n") + insertions[offset];
        }
        String result;
        int cursor = 0;
        for (auto &[offset, value] : insertions) {
            int at = std::clamp(offset, cursor, source.length());
            result += source.substring(cursor, at) + value;
            cursor = at;
        }
        source = result + source.substring(cursor);
        return source;
    }
    juce::StringArray lines;
    std::set<size_t> emitted;
    auto tags = [&](double a, double b) {
        juce::StringArray list;
        for (size_t i = 0; i < events.size(); ++i) {
            auto &c = events[i];
            if (!emitted.count(i) && a <= c.at("start").get<double>() && c.at("start").get<double>() < b &&
                text(c, "name") != "N") {
                list.add("[" + label(c, d) + "]");
                emitted.insert(i);
            }
        }
        return list.joinIntoString(" ");
    };
    auto instrumental = [&](double a, double b) {
        if (b <= a)
            return;
        auto changes = tags(a, b);
        if (changes.isNotEmpty()) {
            lines.add(changes);
        }
    };
    double previousEnd = 0;
    for (auto &cue : d.lyrics()) {
        double start = cue.at("start"), end = cue.at("end");
        instrumental(previousEnd, start);
        auto words = cue.value("words", J::array());
        if (!words.empty()) {
            double first = words.front().at("start"), last = words.back().at("end");
            instrumental(start, first);
            std::map<int, std::vector<String>> anchors;
            for (size_t i = 0; i < events.size(); ++i) {
                auto &chord = events[i];
                double t = chord.at("start");
                if (emitted.count(i) || t < first || t >= last || text(chord, "name") == "N")
                    continue;
                const J *target = nullptr;
                for (auto &w : words)
                    if (w.at("start").get<double>() <= t &&
                        (t < w.at("end").get<double>() ||
                         w.at("start") == w.at("end") && t == w.at("start").get<double>())) {
                        target = &w;
                        break;
                    }
                if (!target)
                    for (auto &w : words)
                        if (w.at("start").get<double>() >= t) {
                            target = &w;
                            break;
                        }
                if (target) {
                    auto &list = anchors[target->at("offset")];
                    auto n = label(chord, d);
                    list.push_back(n);
                    emitted.insert(i);
                }
            }
            lines.add(insertAnchors(text(cue, "text"), anchors));
            instrumental(last, end);
        } else {
            // No word timestamps: only a change exactly at this line's start can
            // be attached to its first character without inventing a word position.
            auto beginning = tags(start, std::nextafter(start, std::numeric_limits<double>::infinity()));
            lines.add(beginning + text(cue, "text"));
            auto changes = tags(start, end);
            if (changes.isNotEmpty())
                lines.add(changes);
        }
        previousEnd = std::max(previousEnd, end);
    }
    instrumental(previousEnd, d.duration());
    if (d.lyrics().empty())
        lines.add(tr("Saisir les paroles ici."));
    return lines.joinIntoString("\n").trim();
}
void updateAutomaticSheet(Document &document, const Document &previous) {
    const auto saved = text(previous.j, "song_sheet");
    const auto baseline = text(previous.j, "song_sheet_generated");
    // Legacy projects have no marker: upgrade only if the saved text exactly
    // matches the generator for the previous data. Otherwise preserve edits.
    const bool automatic =
        saved.isEmpty() ||
        (previous.j.contains("song_sheet_generated") ? saved == baseline : saved == initialSheet(previous));
    if (automatic) {
        const auto generated = utf(initialSheet(document));
        document.j["song_sheet"] = generated;
        document.j["song_sheet_generated"] = generated;
    }
}
std::vector<SheetLine> sheetLines(String source) {
    std::vector<SheetLine> result;
    int base = 0;
    static std::regex chord(
        "[A-G](?:#|b)?(?:(?:maj|min|dim|aug|sus|add|omit|alt|dom|m|M|no)|[0-9#b+()\\-])*(?:/[A-G](?:#|b)?)?");
    for (auto &line : juce::StringArray::fromLines(source)) {
        SheetLine row;
        String pending;
        int textStart = 0;
        for (int at = 0; at < line.length();) {
            if (line[at] == '[') {
                int end = line.indexOfChar(at + 1, ']');
                auto name = end >= 0 ? line.substring(at + 1, end) : String();
                if (end >= 0 && std::regex_match(utf(name), chord)) {
                    if (at > textStart || pending.isNotEmpty())
                        row.tokens.push_back(
                            {line.substring(textStart, at), pending, base + textStart, base + at});
                    pending = name;
                    textStart = end + 1;
                    at = end + 1;
                    continue;
                }
            }
            ++at;
        }
        row.tokens.push_back({line.substring(textStart), pending, base + textStart, base + line.length()});
        result.push_back(row);
        base += line.length() + 1;
    }
    return result;
}
void exportCsv(const Document &d, const File &file) {
    auto quote = [](String s) { return "\"" + s.replace("\"", "\"\"") + "\""; };
    String out = String::charToString(0xfeff) + tr("Début (s);Fin (s);Accord;Basse;Notes MIDI\n");
    for (auto &c : d.chords()) {
        juce::StringArray notes;
        for (int n : c.value("notes", std::vector<int>()))
            notes.add(String(n + d.j.value("transpose", 0)));
        out += String(c.at("start").get<double>(), 3) + ";" + String(c.at("end").get<double>(), 3) + ";" +
               quote(transpose(text(c, "name"), d.j.value("transpose", 0))) + ";" +
               quote(transpose(text(c, "bass"), d.j.value("transpose", 0))) + ";" +
               quote(notes.joinIntoString(" ")) + "\n";
    }
    require(file.replaceWithText(out), tr("Export CSV impossible"));
}
void exportText(const Document &d, const File &file) {
    String out = songTitle(d) + "\n" +
                 (d.j.value("bpm", 0.) > 0 ? tr("Tempo : ") + String(d.j.value("bpm", 0.), 1) + " BPM"
                                           : tr("Tempo non défini")) +
                 tr("\nTonalité : ") + text(d.j, "key") + "\n\n";
    for (auto &c : d.chords()) {
        double time = c.at("start");
        out += String::formatted("%02d:%05.2f  ", (int)time / 60, std::fmod(time, 60.)) + label(c, d) + "\n";
    }
    require(file.replaceWithText(out), tr("Export texte impossible"));
}
void exportChordPro(const Document &d, const File &file) {
    auto source = text(d.j, "song_sheet");
    if (source.isEmpty())
        source = initialSheet(d);
    auto title = songTitle(d).removeCharacters("{}\r\n");
    if (title.isEmpty())
        title = file.getFileNameWithoutExtension();
    require(file.replaceWithText("{title: " + title + "}\n\n" + source +
                                     (source.endsWithChar('\n') ? String() : "\n"),
                                 false, false, "\n"),
            tr("Export ChordPro impossible"));
}
void exportMidi(const Document &d, const File &file) {
    struct Event {
        int64_t time;
        int priority;
        std::vector<uint8_t> bytes;
    };
    double bpm = d.j.value("bpm", 0.);
    if (bpm < 10 || bpm > 400)
        bpm = 120;
    auto vlq = [](uint64_t value) {
        std::vector<uint8_t> result = {(uint8_t)(value & 127)};
        while (value >>= 7)
            result.insert(result.begin(), (uint8_t)((value & 127) | 128));
        return result;
    };
    uint32_t tempo = (uint32_t)(60000000 / bpm);
    std::vector<Event> events = {
        {0, 0, {0xff, 0x51, 3, (uint8_t)(tempo >> 16), (uint8_t)(tempo >> 8), (uint8_t)tempo}},
        {0, 0, {0xc0, 0}}};
    for (auto &c : d.chords()) {
        if (text(c, "name") == "N")
            continue;
        auto name = utf(transpose(text(c, "name"), d.j.value("transpose", 0)));
        std::vector<uint8_t> bytes = {0xff, 1};
        auto size = vlq(name.size());
        bytes.insert(bytes.end(), size.begin(), size.end());
        bytes.insert(bytes.end(), name.begin(), name.end());
        auto start = (int64_t)std::nearbyint(c.at("start").get<double>() * 480 * bpm / 60),
             end = (int64_t)std::nearbyint(c.at("end").get<double>() * 480 * bpm / 60);
        events.push_back({start, 1, bytes});
        std::set<int> seen;
        for (int n : c.value("notes", std::vector<int>())) {
            n += d.j.value("transpose", 0);
            if (n < 0 || n > 127 || !seen.insert(n).second)
                continue;
            events.push_back({start, 2, {0x90, (uint8_t)n, 85}});
            events.push_back({end, 0, {0x80, (uint8_t)n, 0}});
        }
    }
    std::stable_sort(events.begin(), events.end(), [](auto &a, auto &b) {
        return std::tie(a.time, a.priority) < std::tie(b.time, b.priority);
    });
    juce::MemoryOutputStream track;
    int64_t previous = 0;
    for (auto &e : events) {
        auto delta = vlq((uint64_t)std::max<int64_t>(0, e.time - previous));
        track.write(delta.data(), delta.size());
        track.write(e.bytes.data(), e.bytes.size());
        previous = e.time;
    }
    track.write("\0\xff\x2f\0", 4);
    juce::FileOutputStream out(file);
    out.setPosition(0);
    out.truncate();
    require(out.openedOk(), tr("Export MIDI impossible"));
    out.write("MThd", 4);
    out.writeIntBigEndian(6);
    out.writeShortBigEndian(0);
    out.writeShortBigEndian(1);
    out.writeShortBigEndian(480);
    out.write("MTrk", 4);
    out.writeIntBigEndian((int)track.getDataSize());
    out.write(track.getData(), track.getDataSize());
}
namespace {
struct FretboardLayout {
    float width, height = 180, nw, ns, left, top = 22, dx, dy;
    int base = 1;
    explicit FretboardLayout(const std::vector<int> &shape) {
        width = std::max(140.f, 20.f * (shape.size() - 1) + 40);
        nw = 20.f * (shape.size() - 1);
        const float localWidth = width - 12;
        ns = std::min((localWidth - 35) / std::max(1.f, nw), 157.f / 125);
        left = 12 + localWidth / 2 - nw * ns / 2;
        dx = 20 * ns;
        dy = 25 * ns;
        std::vector<int> positive;
        for (int f : shape)
            if (f > 0)
                positive.push_back(f);
        if (!positive.empty() && *std::max_element(positive.begin(), positive.end()) > 5)
            base = std::max(1, *std::min_element(positive.begin(), positive.end()));
    }
    juce::Rectangle<float> content(const std::vector<int> &shape) const {
        auto bounds = juce::Rectangle<float>(left - 1.5f, top - 1.5f, nw * ns + 3, 5 * dy + 3);
        if (base > 1)
            bounds = bounds.getUnion({left - 24, top + dy * .2f, 22, 18});
        for (size_t i = 0; i < shape.size(); ++i) {
            const float x = left + i * dx;
            const int f = shape[i];
            if (f < 0)
                bounds = bounds.getUnion({x - 7, top - 20, 15, 20});
            else if (f == 0)
                bounds = bounds.getUnion({x - 4.5f, top - 14.5f, 9, 9});
            else {
                const float r = std::min(6.f, dx * .3f), y = top + (f - base + .5f) * dy;
                bounds = bounds.getUnion({x - r, y - r, 2 * r, 2 * r});
            }
        }
        return bounds.expanded(2);
    }
};
} // namespace
juce::Rectangle<float> fretboardContentBounds(const std::vector<int> &shape) {
    return shape.empty() ? juce::Rectangle<float>() : FretboardLayout(shape).content(shape);
}
void drawFretboard(juce::Graphics &g, juce::Rectangle<float> rect, const std::vector<int> &shape,
                   bool selected, bool fitContent) {
    if (shape.empty() || rect.isEmpty())
        return;
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(rect.toNearestInt());
    const FretboardLayout layout(shape);
    const auto bounds =
        fitContent ? layout.content(shape) : juce::Rectangle<float>(layout.width, layout.height);
    const float scale = std::min(rect.getWidth() / bounds.getWidth(), rect.getHeight() / bounds.getHeight());
    g.addTransform(juce::AffineTransform::scale(scale).translated(
        rect.getCentreX() - bounds.getCentreX() * scale, rect.getCentreY() - bounds.getCentreY() * scale));
    auto ink = juce::Colour(0xff254962), gold = juce::Colour(0xffad793c);
    if (selected) {
        g.setColour(juce::Colour(0xfffffdf6));
        g.fillRoundedRectangle(0, 0, layout.width, layout.height, 5);
    }
    const auto nw = layout.nw, ns = layout.ns, left = layout.left, top = layout.top, dx = layout.dx,
               dy = layout.dy;
    const int base = layout.base;
    for (size_t i = 0; i < shape.size(); ++i) {
        g.setColour(juce::Colour(0xff9ba9ae));
        g.drawLine(left + i * dx, top, left + i * dx, top + 5 * dy, 1);
    }
    for (int i = 0; i < 6; ++i) {
        g.setColour(ink);
        g.drawLine(left, top + i * dy, left + nw * ns, top + i * dy, i == 0 && base == 1 ? 3.f : 1.f);
    }
    g.setFont(juce::FontOptions("Segoe UI", 13, juce::Font::plain));
    if (base > 1) {
        g.setColour(ink);
        g.drawText(String(base), juce::Rectangle<float>(left - 24, top + dy * .2f, 22, 18),
                   juce::Justification::centred);
    }
    for (size_t i = 0; i < shape.size(); ++i) {
        float x = left + i * dx;
        int fret = shape[i];
        g.setColour(ink);
        if (fret < 0)
            g.drawText(juce::String::fromUTF8("×"), juce::Rectangle<float>(x - 7, top - 20, 15, 20),
                       juce::Justification::centred);
        else if (!fret)
            g.drawEllipse(x - 4, top - 14, 8, 8, 1);
        else {
            float r = std::min(6.f, dx * .3f), y = top + (fret - base + .5f) * dy;
            g.setColour(gold);
            g.fillEllipse(x - r, y - r, 2 * r, 2 * r);
        }
    }
}
void drawPiano(juce::Graphics &g, juce::Rectangle<float> rect, const std::vector<int> &notes,
               const std::vector<int> &left, int pressed, bool showLabels) {
    auto white = [](int n) {
        int p = n % 12;
        return p != 1 && p != 3 && p != 6 && p != 8 && p != 10;
    };
    std::vector<int> keys;
    for (int n = 36; n <= 84; ++n)
        if (white(n))
            keys.push_back(n);
    float width = rect.getWidth() / keys.size(), height = std::min(140.f, rect.getHeight() - 22);
    auto ink = juce::Colour(0xff1d3c53);
    auto active = [&](int n) { return std::find(notes.begin(), notes.end(), n) != notes.end(); };
    auto leftHand = [&](int n) {
        return n != pressed && std::find(left.begin(), left.end(), n) != left.end();
    };
    for (size_t i = 0; i < keys.size(); ++i) {
        auto r = juce::Rectangle<float>(rect.getX() + i * width, rect.getY(), width - 1, height);
        g.setColour(active(keys[i]) ? juce::Colour(leftHand(keys[i]) ? 0xff8fb8c7 : 0xffd2a35e)
                                    : juce::Colour(0xfffffdf6));
        g.fillRoundedRectangle(r, 2);
        g.setColour(juce::Colour(0xffb8c5ca));
        g.drawRoundedRectangle(r, 2, 1);
        if (showLabels && keys[i] % 12 == 0) {
            g.setColour(ink);
            g.setFont(juce::FontOptions(12));
            g.drawText(midiName(keys[i]), r.removeFromBottom(18), juce::Justification::left);
        }
    }
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] < 84 && !white(keys[i] + 1)) {
            auto r = juce::Rectangle<float>(rect.getX() + (i + 1) * width - width * .31f, rect.getY(),
                                            width * .62f, height * .64f);
            g.setColour(active(keys[i] + 1) ? juce::Colour(leftHand(keys[i] + 1) ? 0xff4d8095 : 0xffb8813c)
                                            : ink);
            g.fillRoundedRectangle(r, 2);
        }
}
void exportPdf(const Document &d, Theory &theory, const File &file, bool song) {
    if (!song) {
        exportTimedChart(d, theory, file);
        return;
    }
    NativePdfDocument pdf;
    pdf.setPageSize(595, 842);
    pdf.setTitle(juce::String::fromUTF8("KITHÁRA NÉA"));
    auto page = [&]() {
        auto graphics = pdf.startPage();
        require(graphics != nullptr, tr("Création PDF impossible"));
        graphics->fillAll(juce::Colours::white);
        graphics->setColour(juce::Colour(0xff254962));
        graphics->setFont(juce::FontOptions("Segoe UI", 20, juce::Font::bold));
        graphics->drawText(songTitle(d), 30, 20, 535, 30, juce::Justification::left);
        graphics->setFont(juce::FontOptions("Segoe UI", 11, juce::Font::plain));
        String info =
            d.j.value("bpm", 0.) > 0 ? String(d.j.value("bpm", 0.), 1) + " BPM" : tr("Tempo non déterminé");
        int meter = d.j.value("meter", 0);
        info += tr(" · Mesure ") + (meter ? String(meter) + "/" + String(d.j.value("meter_denominator", 4))
                                          : tr("non déterminée"));
        if (text(d.j, "meter_source") == "estimated")
            info += tr(" (proposée)");
        info += juce::String::fromUTF8(" · Capo ") + String(d.j.value("capo", 0));
        graphics->drawText(info, 30, 48, 535, 16, juce::Justification::left);
        return graphics;
    };
    auto g = page();
    float y = 78;
    std::vector<String> used;
    auto source = text(d.j, "song_sheet");
    if (source.isEmpty())
        source = initialSheet(d);
    auto rows = sheetLines(source);
    if (song) {
        for (auto &row : rows) {
            const bool hasChord = std::any_of(row.tokens.begin(), row.tokens.end(),
                                              [](const auto &token) { return token.chord.isNotEmpty(); });
            const bool hasText = std::any_of(row.tokens.begin(), row.tokens.end(), [](const auto &token) {
                return token.text.trim().isNotEmpty();
            });
            const float rowHeight = hasChord && hasText ? 44.f : 22.f;
            const int chordHeight = hasChord && hasText ? 18 : 0;
            float x = 30;
            if (y > 760) {
                g = page();
                y = 78;
            }
            if (!hasChord && !hasText) {
                y += 22;
                continue;
            }
            std::vector<SheetToken> pieces;
            for (auto &token : row.tokens) {
                int offset = 0;
                bool first = true;
                do {
                    int stop = offset;
                    while (stop < token.text.length() &&
                           !juce::CharacterFunctions::isWhitespace(token.text[stop]))
                        ++stop;
                    while (stop < token.text.length() &&
                           juce::CharacterFunctions::isWhitespace(token.text[stop]))
                        ++stop;
                    pieces.push_back({token.text.substring(offset, stop), first ? token.chord : String(),
                                      token.sourceStart + offset, token.sourceStart + stop});
                    first = false;
                    if (stop == offset)
                        break;
                    offset = stop;
                } while (offset < token.text.length());
            }
            for (auto &token : pieces) {
                juce::Font font(juce::FontOptions("Segoe UI", 14, juce::Font::plain));
                juce::GlyphArrangement glyphs;
                glyphs.addLineOfText(font, token.text, 0, 0);
                float width = std::max(4.f, glyphs.getBoundingBox(0, -1, true).getWidth() + 2);
                juce::GlyphArrangement chordGlyphs;
                chordGlyphs.addLineOfText(juce::Font(juce::FontOptions("Segoe UI", 14, juce::Font::bold)),
                                          token.chord, 0, 0);
                width = std::max(width, chordGlyphs.getBoundingBox(0, -1, true).getWidth() + 8);
                if (x + width > 565 && x > 30) {
                    x = 30;
                    y += rowHeight;
                    if (y > 760) {
                        g = page();
                        y = 78;
                    }
                }
                if (token.chord.isNotEmpty()) {
                    g->setColour(juce::Colour(0xffad793c));
                    g->setFont(juce::FontOptions("Segoe UI", 14, juce::Font::bold));
                    g->drawText(token.chord, (int)x, (int)y, (int)std::max(width, 20.f), 18,
                                juce::Justification::left);
                    if (std::find(used.begin(), used.end(), token.chord) == used.end())
                        used.push_back(token.chord);
                }
                g->setColour(juce::Colour(0xff254962));
                g->setFont(font);
                g->drawText(token.text, (int)x, (int)y + chordHeight, (int)std::max(width, 1.f), 20,
                            juce::Justification::left);
                x += width;
            }
            y += rowHeight;
        }
    }
    if (song) {
        float x = 30;
        y += 15;
        for (auto &n : used) {
            if (y + 145 > 810) {
                g = page();
                y = 78;
            }
            g->setColour(juce::Colour(0xff254962));
            g->setFont(juce::FontOptions("Segoe UI", 14, juce::Font::bold));
            g->drawText(n, (int)x, (int)y, 100, 20, juce::Justification::centred);
            J chord = {{"name", utf(n.upToFirstOccurrenceOf("/", false, false))},
                       {"bass", utf(n.fromFirstOccurrenceOf("/", false, false))}};
            Document visual = d;
            visual.j["transpose"] = 0;
            g->diagram({x, y + 18, 100, 125}, theory.chosen(chord, visual));
            x += 110;
            if (x + 100 > 565) {
                x = 30;
                y += 155;
            }
        }
    }
    juce::FileOutputStream out(file);
    out.setPosition(0);
    out.truncate();
    require(out.openedOk() && pdf.writeToStream(out), tr("Export PDF impossible"));
}
} // namespace kn
