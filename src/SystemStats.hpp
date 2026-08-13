#pragma once

#include <cstdint>

namespace pt {

    /// A CPU/RAM reading. Negative values mean "not available on this platform".
    struct SysSample {
        float procCpu = -1.f; ///< this process, % of the whole machine
        float sysCpu  = -1.f; ///< whole system, %
        int   ramMB   = -1;   ///< this process, resident memory in MB
    };

    /// CPU / memory counters for the current process.
    ///
    /// Windows and Linux/Android are implemented. On macOS/iOS the counters are
    /// reported as unavailable rather than guessed - see SystemStats.cpp.
    class SystemStats {
    public:
        static SystemStats& get();

        /// Reading averaged over the time elapsed since the previous call.
        /// The first call after construction/reset returns CPU as unavailable
        /// (there is no interval to average over yet) but RAM is already valid.
        SysSample sample();

        /// Drop the previous counters, so the next sample starts a fresh interval.
        void reset();

        int coreCount() const { return m_cores; }
        static bool cpuSupported();
        static bool ramSupported();

    private:
        SystemStats();

        int      m_cores        = 1;
        bool     m_hasPrev      = false;
        uint64_t m_prevProc     = 0;
        uint64_t m_prevWall     = 0;
        uint64_t m_prevSysIdle  = 0;
        uint64_t m_prevSysTotal = 0;
    };
}
