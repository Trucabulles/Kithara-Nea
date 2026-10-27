#include "Core.h"
#include <numeric>
namespace kn {
namespace {
struct RhythmWindow {
    std::vector<double> beat, down;
    double energy = 0;
};
std::vector<double> correlations(const std::vector<float> &values, int begin, int end, int maximum) {
    std::vector<double> out((size_t)maximum + 1);
    double mean = std::accumulate(values.begin() + begin, values.begin() + end, 0.) / (end - begin);
    for (int lag = 1; lag <= maximum && lag < end - begin - 2; ++lag) {
        double xy = 0, xx = 0, yy = 0;
        for (int n = begin + lag; n < end; ++n) {
            double a = values[(size_t)n] - mean, b = values[(size_t)n - lag] - mean;
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }
        out[(size_t)lag] = xx * yy > 1e-12 ? xy / std::sqrt(xx * yy) : 0;
    }
    return out;
}
double at(const std::vector<double> &v, double lag) {
    if (lag < 1 || lag >= v.size() - 1)
        return 0;
    int n = (int)lag;
    return v[(size_t)n] * (1 - (lag - n)) + v[(size_t)n + 1] * (lag - n);
}
double median(std::vector<double> v) {
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
} // namespace
void estimateRhythmActivations(Document &document, const std::vector<float> &beat,
                               const std::vector<float> &down, Cancel cancel) {
    require(beat.size() == down.size(), "Dimensions rythmiques incompatibles");
    const int count = (int)beat.size();
    const bool manualBpm = text(document.j, "bpm_source") == "manual";
    const bool manualMeter = text(document.j, "meter_source") == "manual";
    double bpm = 0, confidence = 0;
    int meter = 0;
    double meterScore = 0;
    std::vector<RhythmWindow> windows;
    const int length = std::min(1200, count);
    if (count >= 200 && *std::max_element(beat.begin(), beat.end()) > .15f) {
        for (int begin = 0; begin + length <= count; begin += std::max(1, length / 2)) {
            checkCancel(cancel);
            int end = begin + length;
            double mean = std::accumulate(beat.begin() + begin, beat.begin() + end, 0.) / length,
                   variance = 0;
            for (int n = begin; n < end; ++n)
                variance += std::pow(beat[(size_t)n] - mean, 2);
            if (variance / length < 1e-5)
                continue;
            RhythmWindow w;
            w.energy = std::sqrt(variance / length);
            w.beat = correlations(beat, begin, end, 150);
            w.down = correlations(down, begin, end, 620);
            windows.push_back(std::move(w));
        }
    }
    std::vector<double> aggregate(151, 0);
    double weight = 0;
    for (auto &w : windows) {
        weight += w.energy;
        for (int lag = 13; lag <= 150; ++lag)
            aggregate[(size_t)lag] += std::max(0., w.beat[(size_t)lag]) * w.energy;
    }
    if (weight > 0)
        for (auto &value : aggregate)
            value /= weight;
    int best = 0;
    for (int lag = 13; lag < 150; ++lag)
        if (aggregate[(size_t)lag] >= aggregate[(size_t)lag - 1] &&
            aggregate[(size_t)lag] >= aggregate[(size_t)lag + 1] &&
            (!best || aggregate[(size_t)lag] > aggregate[(size_t)best]))
            best = lag;
    // A bar can correlate more strongly than its individual beats. Prefer a shorter supported
    // period only when it divides that period and retains at least 80% of the evidence.
    if (best) {
        int strongest = best;
        for (int lag = 13; lag < strongest; ++lag) {
            double multiple = (double)strongest / lag;
            if (aggregate[(size_t)lag] >= aggregate[(size_t)strongest] * .80 &&
                aggregate[(size_t)lag] >= aggregate[(size_t)lag - 1] &&
                aggregate[(size_t)lag] >= aggregate[(size_t)lag + 1] && std::round(multiple) >= 2 &&
                std::round(multiple) <= 8 && std::abs(multiple - std::round(multiple)) < .12) {
                best = lag;
                break;
            }
        }
    }
    // When the independently detected peaks are already regular, retain their supported pulse
    // rather than substituting a slower accent/bar. Weak or irregular peak trains get no such vote.
    auto times = document.j.value("beats", J::array());
    if (times.size() >= 8) {
        std::vector<double> intervals;
        for (size_t n = 1; n < times.size(); ++n)
            intervals.push_back(times[n].get<double>() - times[n - 1].get<double>());
        double pulse = median(intervals), regular = 0;
        if (pulse > 0)
            for (double value : intervals)
                if (std::abs(value / pulse - 1) < .18)
                    regular++;
        int lag = (int)std::round(pulse * 50);
        if (regular / intervals.size() >= .65 && lag >= 13 && lag < 150 && aggregate[(size_t)lag] > .10 &&
            aggregate[(size_t)lag] >= aggregate[(size_t)best] * .5)
            best = lag;
    }
    std::vector<std::pair<double, double>> periods;
    J candidates = J::array();
    if (best && aggregate[(size_t)best] >= .12) {
        for (auto &w : windows) {
            int local = best;
            for (int lag = std::max(13, (int)(best * .90)); lag <= std::min(149, (int)(best * 1.10)); ++lag)
                if (w.beat[(size_t)lag] > w.beat[(size_t)local])
                    local = lag;
            double score = w.beat[(size_t)local];
            if (score < .16)
                continue;
            double a = w.beat[(size_t)local - 1], b = w.beat[(size_t)local], c = w.beat[(size_t)local + 1];
            double fine =
                local +
                std::clamp(std::abs(a - 2 * b + c) > 1e-9 ? .5 * (a - c) / (a - 2 * b + c) : 0., -.5, .5);
            periods.push_back({fine, score * w.energy});
        }
        confidence = (double)periods.size() / std::max<size_t>(1, windows.size());
        if (periods.size() >= std::min<size_t>(3, windows.size()) && confidence >= .4) {
            std::sort(periods.begin(), periods.end());
            double sum = 0, total = 0, period = best;
            for (auto v : periods)
                total += v.second;
            for (auto v : periods) {
                sum += v.second;
                if (sum >= total * .5) {
                    period = v.first;
                    break;
                }
            }
            bpm = 3000 / period;
            double scores[2] = {0, 0};
            int supported[2] = {0, 0};
            for (auto &w : windows) {
                int local = std::clamp((int)std::round(period), 13, 149);
                for (int lag = std::max(13, (int)(period * .90)); lag <= std::min(149, (int)(period * 1.10));
                     ++lag)
                    if (w.beat[(size_t)lag] > w.beat[(size_t)local])
                        local = lag;
                if (w.beat[(size_t)local] < .16)
                    continue;
                for (int m = 0; m < 2; ++m) {
                    // Count a bar as three or four locally coherent pulses, allowing small timing variation.
                    double score = -1;
                    for (double delta = -2; delta <= 2; delta += .5)
                        score = std::max(score, at(w.down, local * (m + 3) + delta));
                    scores[m] += score * w.energy;
                    if (score > .07)
                        ++supported[m];
                }
            }
            scores[0] /= weight;
            scores[1] /= weight;
            int selected = scores[1] > scores[0] ? 1 : 0;
            meterScore = scores[selected];
            if (scores[selected] > .06 && scores[selected] - scores[1 - selected] > .025 &&
                supported[selected] >= std::min(3, (int)windows.size()) &&
                (double)supported[selected] / windows.size() >= .35)
                meter = selected + 3;
        }
    }
    for (int lag = 13; lag < 150; ++lag)
        if (aggregate[(size_t)lag] > .1 && aggregate[(size_t)lag] >= aggregate[(size_t)lag - 1] &&
            aggregate[(size_t)lag] >= aggregate[(size_t)lag + 1])
            candidates.push_back({{"bpm", 3000. / lag}, {"score", aggregate[(size_t)lag]}});
    std::sort(candidates.begin(), candidates.end(), [](auto &a, auto &b) {
        return a.at("score").template get<double>() > b.at("score").template get<double>();
    });
    if (candidates.size() > 5)
        candidates.erase(candidates.begin() + 5, candidates.end());
    document.j["rhythm_estimate"] = {{"method", "activation-periodicity-v1"},
                                     {"bpm", bpm},
                                     {"meter", meter},
                                     {"meter_denominator", 4},
                                     {"window_support", confidence},
                                     {"meter_score", meterScore},
                                     {"windows", windows.size()},
                                     {"tempo_candidates", candidates},
                                     {"scores_are_probabilities", false}};
    if (!manualBpm) {
        document.j["bpm"] = bpm;
        document.j["bpm_source"] = bpm > 0 ? "estimated" : "unknown";
    }
    if (!manualMeter) {
        document.j["meter"] = meter;
        document.j["meter_denominator"] = 4;
        document.j["meter_source"] = meter ? "estimated" : "unknown";
    }
}
} // namespace kn
