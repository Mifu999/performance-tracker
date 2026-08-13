#pragma once

#include "Types.hpp"

#include <map>
#include <vector>

namespace pt {

    struct LevelStat {
        int32_t     id           = 0;
        double      seconds      = 0;
        double      fpsWeighted  = 0;
        double      low1Weighted = 0;
        float       fpsMin       = 0.f;
        float       fpsMax       = 0.f;
        size_t      samples      = 0;
        std::time_t worstAt      = 0;

        float avgFps()  const { return seconds > 0 ? static_cast<float>(fpsWeighted / seconds) : 0.f; }
        float avgLow1() const { return seconds > 0 ? static_cast<float>(low1Weighted / seconds) : 0.f; }
    };

    struct HourStat {
        double seconds     = 0;
        double fpsWeighted = 0;
        float  fpsMin      = 0.f;
        float  fpsMax      = 0.f;

        float avgFps() const { return seconds > 0 ? static_cast<float>(fpsWeighted / seconds) : 0.f; }
    };

    struct Aggregate {
        size_t   samples     = 0;
        double   seconds     = 0;
        uint64_t totalFrames = 0;

        double fpsWeighted  = 0;
        double low1Weighted = 0;

        float       fpsMin      = 0.f;
        std::time_t fpsMinAt    = 0;
        int32_t     fpsMinLevel = 0;
        float       fpsMax      = 0.f;
        std::time_t fpsMaxAt    = 0;
        int32_t     fpsMaxLevel = 0;

        double      cpuWeighted = 0, cpuSeconds = 0;
        float       cpuMax      = -1.f;
        std::time_t cpuMaxAt    = 0;
        double      sysWeighted = 0, sysSeconds = 0;
        double      ramWeighted = 0, ramSeconds = 0;
        int         ramMax      = -1;
        std::time_t ramMaxAt    = 0;

        double secondsBelow30     = 0;
        double secondsBelow60     = 0;
        double secondsBelowTarget = 0;
        double targetSeconds      = 0;

        double   secondsByContext[kContextCount] = {};
        HourStat hours[24];

        std::map<int32_t, LevelStat> levels;

        bool  empty()   const { return samples == 0; }
        float avgFps()  const { return seconds > 0 ? static_cast<float>(fpsWeighted / seconds) : 0.f; }
        float avgLow1() const { return seconds > 0 ? static_cast<float>(low1Weighted / seconds) : 0.f; }
        float avgCpu()  const { return cpuSeconds > 0 ? static_cast<float>(cpuWeighted / cpuSeconds) : -1.f; }
        float avgSys()  const { return sysSeconds > 0 ? static_cast<float>(sysWeighted / sysSeconds) : -1.f; }
        float avgRam()  const { return ramSeconds > 0 ? static_cast<float>(ramWeighted / ramSeconds) : -1.f; }

        float pctBelow30()     const { return seconds > 0 ? static_cast<float>(secondsBelow30 / seconds * 100.0) : 0.f; }
        float pctBelow60()     const { return seconds > 0 ? static_cast<float>(secondsBelow60 / seconds * 100.0) : 0.f; }
        float pctBelowTarget() const { return targetSeconds > 0 ? static_cast<float>(secondsBelowTarget / targetSeconds * 100.0) : -1.f; }
    };

    Aggregate aggregate(std::vector<Sample> const& samples);

    /// One drawable column of a graph.
    struct GraphPoint {
        bool   has = false;
        double t   = 0;   ///< middle of the column, unix seconds
        float  avg = 0.f;
        float  lo  = 0.f;
        float  hi  = 0.f;
    };

    enum class Metric {
        Fps       = 0,
        CpuProc   = 1,
        Ram       = 2,
        CpuSystem = 3,
    };

    char const* metricName(Metric m);
    char const* metricUnit(Metric m);

    std::vector<GraphPoint> buildSeries(
        std::vector<Sample> const& samples,
        std::time_t from, std::time_t to,
        size_t columns, Metric metric
    );
}
