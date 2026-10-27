#include "Core.h"
#include "Pdf.h"
namespace kn {
namespace {
const juce::Colour ink(0xff254962), border(0xffcbbda5), accent(0xffad793c);
float textWidth(const String &value, float size = 15) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions("Segoe UI", size, juce::Font::bold)), value, 0, 0);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}
void text(PdfCanvas *g, const String &value, float x, float y, float width, float size = 12,
          bool bold = false) {
    g->setColour(ink);
    g->setFont(juce::FontOptions("Segoe UI", size, bold ? juce::Font::bold : juce::Font::plain));
    g->drawText(value, (int)x, (int)y, (int)std::ceil(width), (int)std::ceil(size + 6),
                juce::Justification::centredLeft);
}
std::vector<double> times(const Document &d, const char *key) {
    std::vector<double> result;
    double previous = -1;
    for (const auto &value : d.j.value(key, J::array())) {
        if (!value.is_number())
            return {};
        double t = value.get<double>();
        if (!std::isfinite(t) || t < 0 || t <= previous || t > d.duration() + 1e-6)
            return {};
        result.push_back(t);
        previous = t;
    }
    return result;
}
double coordinate(const J &bar, double time) {
    const auto &pulses = bar.at("pulses");
    const double start = bar.at("start"), end = bar.at("end");
    if (pulses.size() < 2)
        return std::clamp((time - start) / (end - start), 0., 1.);
    for (size_t i = 1; i < pulses.size(); ++i) {
        double a = pulses[i - 1], b = pulses[i];
        if (time <= b)
            return std::clamp((i - 1 + (time - a) / (b - a)) / (pulses.size() - 1), 0., 1.);
    }
    return 1.;
}
constexpr int chartColumns = 3;
constexpr float chartCellWidth = 511.f / chartColumns, chartInset = 8, labelGap = 5;
struct LabelBox {
    int fragment;
    float x, width;
};
String chordName(const J &fragment) {
    return kn::text(fragment, fragment.contains("display_name") ? "display_name" : "name");
}
float requiredWidth(const J &bar, float size) {
    float total = 0;
    int count = 0;
    for (const auto &fragment : bar.at("fragments"))
        if (kn::text(fragment, "name") != "N") {
            total += textWidth(chordName(fragment), size) + 3;
            ++count;
        }
    return total + std::max(0, count - 1) * labelGap;
}
float chartFontSize(const J &bars) {
    // One font for the complete chart, never one size per cell or per chord.
    for (float size = 12; size >= 10.5f; size -= .25f) {
        bool fits = true;
        for (const auto &bar : bars)
            fits &= requiredWidth(bar, size) <= chartCellWidth - 2 * chartInset;
        if (fits)
            return size;
    }
    return 0;
}
std::vector<LabelBox> labels(const J &row, float origin, float available, float size) {
    std::vector<LabelBox> result;
    float cursor = origin;
    for (size_t i = 0; i < row.at("fragments").size(); ++i) {
        const auto &fragment = row.at("fragments")[i];
        if (kn::text(fragment, "name") == "N")
            continue;
        const float width = textWidth(chordName(fragment), size) + 3;
        const float desired = origin + available * fragment.at("x0").get<float>();
        const float x = std::max(cursor, desired);
        result.push_back({(int)i, x, width});
        cursor = x + width + labelGap;
    }
    // Push a cluster left from the right edge, retaining order and full text widths.
    cursor = origin + available;
    for (auto it = result.rbegin(); it != result.rend(); ++it) {
        it->x = std::min(it->x, cursor - it->width);
        cursor = it->x - labelGap;
    }
    return result;
}
} // namespace

J timedChartLayout(const Document &d, Theory &theory) {
    J result = {{"mode", "unavailable"},
                {"reason", "missing-rhythm"},
                {"bars", J::array()},
                {"dictionary", J::array()},
                {"events", J::array()}};
    std::map<std::string, int> diagramIds;
    for (const auto &chord : d.chords()) {
        double start = chord.at("start"), end = chord.at("end");
        if (end <= start)
            continue;
        J event = {{"start", start}, {"end", end}, {"name", utf(label(chord, d))}, {"diagram", -1}};
        if (kn::text(chord, "name") != "N") {
            auto shape = theory.chosen(chord, d);
            auto fingerprint =
                J::array({label(chord, d).toStdString(), kn::text(d.j, "guitar_tuning").toStdString(),
                          d.j.value("capo", 0), shape})
                    .dump();
            auto found = diagramIds.find(fingerprint);
            int id = found == diagramIds.end() ? (int)diagramIds.size() : found->second;
            if (found == diagramIds.end()) {
                diagramIds[fingerprint] = id;
                auto candidates =
                    theory.positions(transpose(kn::text(chord, "name"), d.j.value("transpose", 0)),
                                     kn::text(d.j, "guitar_tuning"), d.j.value("capo", 0));
                auto chosen = std::find(candidates.begin(), candidates.end(), shape);
                result["dictionary"].push_back(
                    {{"name", event["name"]},
                     {"shape", shape},
                     {"position",
                      chosen == candidates.end() ? 0 : (int)std::distance(candidates.begin(), chosen) + 1},
                     {"tuning", d.j.at("guitar_tuning")},
                     {"capo", d.j.value("capo", 0)}});
            }
            event["diagram"] = id;
        }
        result["events"].push_back(event);
    }
    std::stable_sort(result["events"].begin(), result["events"].end(), [](const auto &a, const auto &b) {
        return a.at("start").template get<double>() < b.at("start").template get<double>();
    });
    double previousEnd = 0;
    for (const auto &event : result["events"]) {
        if (event.at("start").get<double>() < previousEnd - 1e-7) {
            result["reason"] = "overlapping-chords";
            return result;
        }
        previousEnd = event.at("end");
    }
    std::map<std::string, int> nameCounts, namePositions;
    for (const auto &entry : result["dictionary"])
        nameCounts[entry.at("name").get<std::string>()]++;
    for (auto &entry : result["dictionary"]) {
        auto name = entry.at("name").get<std::string>();
        entry["reference"] =
            nameCounts[name] > 1 ? name + " (" + std::to_string(++namePositions[name]) + ")" : name;
    }
    for (auto &event : result["events"])
        if (event.at("diagram").get<int>() >= 0)
            event["display_name"] = result["dictionary"][event.at("diagram").get<int>()].at("reference");
    const int meter = d.j.value("meter", 0), denominator = d.j.value("meter_denominator", 4);
    const auto beats = times(d, "beats"), down = times(d, "downbeats");
    // Prefer exact saved measures, but do not replace a chart with a timestamp list.
    if (meter < 2 || meter > 12 || denominator != 4 || down.empty())
        return result;
    J bars = J::array();
    int number = 1;
    auto add = [&](double start, double end, String kind, const std::vector<double> &pulses) {
        bars.push_back({{"number", kind == "prelude" ? 0 : number++},
                        {"kind", utf(kind)},
                        {"start", start},
                        {"end", end},
                        {"pulses", pulses},
                        {"fragments", J::array()}});
    };
    bool coherent = beats.size() >= 3 && down.size() >= 2;
    if (down.front() > 1e-6)
        add(0, down.front(), "prelude", {});
    for (size_t i = 1; coherent && i < down.size(); ++i) {
        const double start = down[i - 1], end = down[i];
        std::vector<double> pulses{start};
        for (double t : beats)
            if (t > start + .03 && t < end - .03)
                pulses.push_back(t);
        pulses.push_back(end);
        coherent = pulses.size() == (size_t)meter + 1;
        double minGap = end - start, maxGap = 0;
        for (size_t p = 1; p < pulses.size(); ++p) {
            minGap = std::min(minGap, pulses[p] - pulses[p - 1]);
            maxGap = std::max(maxGap, pulses[p] - pulses[p - 1]);
        }
        coherent &= minGap > .08 && maxGap <= minGap * 2.5;
        for (double endpoint : {start, end})
            coherent &= std::any_of(beats.begin(), beats.end(),
                                    [&](double t) { return std::abs(t - endpoint) <= .03; });
        if (coherent)
            add(start, end, "measure", pulses);
    }
    if (coherent && d.duration() > down.back() + 1e-6) {
        size_t count = 0;
        for (double t : beats)
            count += t > down.back() + .03 && t < d.duration() - .03;
        coherent = count < (size_t)meter;
        if (coherent)
            add(down.back(), d.duration(), "partial", {});
    }
    if (!coherent) {
        const double bpm = d.j.value("bpm", 0.);
        if (!std::isfinite(bpm) || bpm < 10 || bpm > 600)
            return result;
        // Explicit estimated constant-tempo chart: BPM + signature, anchored to the first
        // detected downbeat. Never move source chords or pretend this is a validated tempo map.
        bars.clear();
        number = 1;
        const double beatDuration = 60. / bpm, barDuration = meter * beatDuration;
        if (down.front() > 1e-6)
            add(0, down.front(), "prelude", {});
        for (int n = 0; n < 10000; ++n) {
            const double start = down.front() + n * barDuration;
            if (start >= d.duration() - 1e-6)
                break;
            const double end = std::min(d.duration(), start + barDuration);
            std::vector<double> pulses;
            if (end >= start + barDuration - 1e-6)
                for (int beat = 0; beat <= meter; ++beat)
                    pulses.push_back(start + beat * beatDuration);
            add(start, end, pulses.empty() ? "partial" : "measure", pulses);
        }
        result["reason"] = "estimated-constant-tempo";
    } else {
        result["reason"] = "existing-grid";
    }
    for (auto &bar : bars) {
        double cursor = bar.at("start"), end = bar.at("end");
        auto fragment = [&](double a, double b, const J *event) {
            if (b <= a)
                return;
            J part = event ? *event : J{{"name", "N"}, {"diagram", -1}};
            part["source_start"] = event ? event->at("start") : J(a);
            part["source_end"] = event ? event->at("end") : J(b);
            part["continued"] = event && event->at("start").get<double>() < a - 1e-7;
            part["start"] = a;
            part["end"] = b;
            part["x0"] = coordinate(bar, a);
            part["x1"] = coordinate(bar, b);
            bar["fragments"].push_back(part);
        };
        for (const auto &event : result["events"]) {
            double a = std::max(bar.at("start").get<double>(), event.at("start").get<double>());
            double b = std::min(end, event.at("end").get<double>());
            if (b <= a)
                continue;
            fragment(cursor, a, nullptr);
            fragment(a, b, &event);
            cursor = b;
        }
        fragment(cursor, end, nullptr);
    }
    result["mode"] = "measures";
    result["bars"] = std::move(bars);
    const float size = chartFontSize(result["bars"]);
    result["print"] = {{"columns", chartColumns}, {"font_size", size}, {"bars", J::array()}};
    if (size > 0)
        for (const auto &bar : result["bars"]) {
            J printed = J::array();
            for (const auto &item : labels(bar, chartInset, chartCellWidth - 2 * chartInset, size))
                printed.push_back({{"fragment", item.fragment},
                                   {"x", item.x},
                                   {"width", item.width},
                                   {"baseline", 28},
                                   {"font_size", size}});
            result["print"]["bars"].push_back(printed);
        }
    return result;
}

void exportTimedChart(const Document &d, Theory &theory, const File &file) {
    const auto model = timedChartLayout(d, theory);
    require(model.at("mode") == "measures",
            tr("Grille de mesures indisponible : vérifier le tempo, la mesure et les premiers temps."));
    const float chordFont = model.at("print").at("font_size");
    require(chordFont > 0, tr("Trop d’accords dans une mesure pour une ligne lisible."));
    NativePdfDocument pdf;
    std::vector<PdfCanvas *> pages;
    auto page = [&]() {
        auto *g = pdf.startPage();
        pages.push_back(g);
        g->fillAll(juce::Colours::white);
        auto title = d.audioName.isNotEmpty() ? File(d.audioName).getFileNameWithoutExtension()
                                              : d.audio().getFileNameWithoutExtension();
        text(g, title, 42, 28, 511, 20, true);
        text(g, tr("Grille d’accords par mesures"), 42, 55, 511, 12);
        String info =
            d.j.value("bpm", 0.) > 0 ? String(d.j.value("bpm", 0.), 1) + " BPM" : tr("Tempo non déterminé");
        info += juce::String::fromUTF8(" · ") +
                (d.j.value("meter", 0) > 0
                     ? String(d.j.value("meter", 0)) + "/" + String(d.j.value("meter_denominator", 4))
                     : tr("Mesure non déterminée"));
        info += juce::String::fromUTF8(" · Capo ") + String(d.j.value("capo", 0));
        if (kn::text(d.j, "meter_source") == "estimated")
            info += tr(" (proposée)");
        text(g, info, 42, 74, 511, 10);
        if (model.at("reason") == "estimated-constant-tempo")
            text(g, tr("Grille estimée à tempo constant ; premier temps détecté comme origine."), 42, 90, 511,
                 9);
        return g;
    };
    auto *g = page();
    float y = 112;
    constexpr int columns = chartColumns;
    constexpr float cellWidth = chartCellWidth;
    const auto &bars = model.at("bars");
    for (size_t first = 0; first < bars.size(); first += columns) {
        constexpr float height = 64;
        std::vector<std::vector<LabelBox>> rowLabels;
        for (size_t col = 0; col < columns && first + col < bars.size(); ++col)
            rowLabels.push_back(labels(bars[first + col], 42 + col * cellWidth + chartInset,
                                       cellWidth - 2 * chartInset, chordFont));
        if (y + height > 788) {
            g = page();
            y = 112;
        }
        for (size_t col = 0; col < rowLabels.size(); ++col) {
            const auto &bar = bars[first + col];
            const float x = 42 + col * cellWidth;
            g->setColour(border);
            g->rectangle(x, y, cellWidth, height, false);
            auto kind = kn::text(bar, "kind");
            String title =
                kind == "prelude" ? tr("Préambule") : tr("Mesure ") + String(bar.at("number").get<int>());
            if (kind == "partial")
                title += " *";
            text(g, title, x + 8, y + 4, cellWidth - 16, 10);
            for (const auto &item : rowLabels[col]) {
                const auto &part = bar.at("fragments")[(size_t)item.fragment];
                auto name = kn::text(part, part.contains("display_name") ? "display_name" : "name");
                // A held chord appears again in each occupied measure, without fake events.
                text(g, name, item.x, y + 28, item.width, chordFont, true);
            }
        }
        y += height;
    }
    if (!model.at("dictionary").empty()) {
        g = page();
        text(g, tr("Dictionnaire des accords"), 42, 104, 511, 17, true);
        y = 140;
        int column = 0;
        for (const auto &entry : model.at("dictionary")) {
            if (y + 160 > 788) {
                g = page();
                y = 140;
                text(g, tr("Dictionnaire des accords"), 42, 104, 511, 17, true);
            }
            float x = 42.f + column * 128;
            text(g, kn::text(entry, entry.contains("reference") ? "reference" : "name"), x, y, 118, 15, true);
            const auto shape = entry.at("shape").get<std::vector<int>>();
            if (shape.empty())
                text(g, tr("Sans position"), x, y + 35, 118, 10);
            else
                g->diagram({x, y + 21, 118, 119}, shape);
            text(g, tr("Position ") + String(entry.at("position").get<int>()), x, y + 143, 118, 10);
            if (++column == 4) {
                column = 0;
                y += 170;
            }
        }
    }
    for (size_t i = 0; i < pages.size(); ++i)
        text(pages[i], String((int)i + 1) + " / " + String((int)pages.size()), 490, 809, 63, 10);
    juce::FileOutputStream stream(file);
    stream.setPosition(0);
    stream.truncate();
    require(stream.openedOk() && pdf.writeToStream(stream), tr("Export PDF impossible"));
}
} // namespace kn
