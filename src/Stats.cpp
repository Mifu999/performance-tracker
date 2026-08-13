#include "Stats.hpp"

#include <algorithm>

using namespace pt;

Aggregate pt::aggregate(std::vector<Sample> const& samples) {
    Aggregate agg;
    bool first = true;

    for (auto const& s : samples) {
        if (s.duration == 0) continue;

        double dur = s.duration;
        auto   t   = static_cast<std::time_t>(s.time);

        agg.samples += 1;
        agg.seconds += dur;
        agg.totalFrames += s.frames;
        agg.fpsWeighted  += static_cast<double>(s.fpsAvg) * dur;
        agg.low1Weighted += static_cast<double>(s.fpsLow1) * dur;

        if (first || s.fpsMin < agg.fpsMin) {
            agg.fpsMin      = s.fpsMin;
            agg.fpsMinAt    = t;
            agg.fpsMinLevel = s.levelID;
        }
        if (first || s.fpsMax > agg.fpsMax) {
            agg.fpsMax      = s.fpsMax;
            agg.fpsMaxAt    = t;
            agg.fpsMaxLevel = s.levelID;
        }
        first = false;

        if (hasCpu(s)) {
            float cpu = cpuPercent(s.cpuProc);
            agg.cpuWeighted += static_cast<double>(cpu) * dur;
            agg.cpuSeconds  += dur;
            if (cpu > agg.cpuMax) {
                agg.cpuMax   = cpu;
                agg.cpuMaxAt = t;
            }
        }
        if (hasSysCpu(s)) {
            agg.sysWeighted += static_cast<double>(cpuPercent(s.cpuSys)) * dur;
            agg.sysSeconds  += dur;
        }
        if (hasRam(s)) {
            agg.ramWeighted += static_cast<double>(s.ramMB) * dur;
            agg.ramSeconds  += dur;
            if (static_cast<int>(s.ramMB) > agg.ramMax) {
                agg.ramMax   = static_cast<int>(s.ramMB);
                agg.ramMaxAt = t;
            }
        }

        if (s.fpsAvg < 30.f) agg.secondsBelow30 += dur;
        if (s.fpsAvg < 60.f) agg.secondsBelow60 += dur;
        if (s.targetFps > 0) {
            agg.targetSeconds += dur;
            // 5% tolerance: a 240 target rarely sits exactly on 240
            if (s.fpsAvg < s.targetFps * 0.95f) agg.secondsBelowTarget += dur;
        }

        if (s.context < kContextCount) agg.secondsByContext[s.context] += dur;

        std::tm tm = localTime(t);
        int hour = std::clamp(tm.tm_hour, 0, 23);
        auto& h = agg.hours[hour];
        if (h.seconds == 0) {
            h.fpsMin = s.fpsMin;
            h.fpsMax = s.fpsMax;
        }
        else {
            h.fpsMin = std::min(h.fpsMin, s.fpsMin);
            h.fpsMax = std::max(h.fpsMax, s.fpsMax);
        }
        h.seconds     += dur;
        h.fpsWeighted += static_cast<double>(s.fpsAvg) * dur;

        if (s.levelID != 0) {
            auto& lv = agg.levels[s.levelID];
            if (lv.samples == 0) {
                lv.id      = s.levelID;
                lv.fpsMin  = s.fpsMin;
                lv.fpsMax  = s.fpsMax;
                lv.worstAt = t;
            }
            else {
                if (s.fpsMin < lv.fpsMin) {
                    lv.fpsMin  = s.fpsMin;
                    lv.worstAt = t;
                }
                lv.fpsMax = std::max(lv.fpsMax, s.fpsMax);
            }
            lv.samples      += 1;
            lv.seconds      += dur;
            lv.fpsWeighted  += static_cast<double>(s.fpsAvg) * dur;
            lv.low1Weighted += static_cast<double>(s.fpsLow1) * dur;
        }
    }

    return agg;
}

char const* pt::metricName(Metric m) {
    switch (m) {
        case Metric::CpuProc:   return "CPU (game)";
        case Metric::CpuSystem: return "CPU (system)";
        case Metric::Ram:       return "RAM";
        default:                return "FPS";
    }
}

char const* pt::metricUnit(Metric m) {
    switch (m) {
        case Metric::CpuProc:
        case Metric::CpuSystem: return "%";
        case Metric::Ram:       return "MB";
        default:                return "";
    }
}

std::vector<GraphPoint> pt::buildSeries(
    std::vector<Sample> const& samples,
    std::time_t from, std::time_t to,
    size_t columns, Metric metric
) {
    std::vector<GraphPoint> out(columns);
    if (columns == 0 || to <= from) return out;

    double span  = static_cast<double>(to - from);
    double width = span / static_cast<double>(columns);

    std::vector<double> weight(columns, 0.0);
    std::vector<double> sum(columns, 0.0);

    for (size_t i = 0; i < columns; ++i) {
        out[i].t = static_cast<double>(from) + width * (static_cast<double>(i) + 0.5);
    }

    for (auto const& s : samples) {
        float value = 0.f, lo = 0.f, hi = 0.f;

        switch (metric) {
            case Metric::CpuProc:
                if (!hasCpu(s)) continue;
                value = lo = hi = cpuPercent(s.cpuProc);
                break;
            case Metric::CpuSystem:
                if (!hasSysCpu(s)) continue;
                value = lo = hi = cpuPercent(s.cpuSys);
                break;
            case Metric::Ram:
                if (!hasRam(s)) continue;
                value = lo = hi = static_cast<float>(s.ramMB);
                break;
            default:
                value = s.fpsAvg;
                lo    = s.fpsMin;
                hi    = s.fpsMax;
                break;
        }

        auto   t   = static_cast<double>(s.time);
        double rel = (t - static_cast<double>(from)) / width;
        if (rel < 0) continue;
        auto idx = static_cast<size_t>(rel);
        if (idx >= columns) idx = columns - 1;

        double w = s.duration > 0 ? s.duration : 1.0;
        auto& p = out[idx];
        if (!p.has) {
            p.has = true;
            p.lo  = lo;
            p.hi  = hi;
        }
        else {
            p.lo = std::min(p.lo, lo);
            p.hi = std::max(p.hi, hi);
        }
        sum[idx]    += static_cast<double>(value) * w;
        weight[idx] += w;
    }

    for (size_t i = 0; i < columns; ++i) {
        if (out[i].has && weight[i] > 0) {
            out[i].avg = static_cast<float>(sum[i] / weight[i]);
        }
    }
    return out;
}
