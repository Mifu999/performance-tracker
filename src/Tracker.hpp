#pragma once

#include "SystemStats.hpp"
#include "Types.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace pt {

    /// Measures frame times and turns them into Samples.
    ///
    /// onFrame() is called once per rendered frame from a CCScheduler::update
    /// hook. Frame times come from steady_clock rather than GD's delta time,
    /// which is smoothed and clamped and would hide real stutters.
    class Tracker {
    public:
        static Tracker& get();

        void onLoad();
        void onFrame();
        void onExit();

        // live values, for the overlay
        float      liveFps()         const { return m_liveFps; }
        float      liveFrameTimeMs() const { return m_liveFrameMs; }
        SysSample  liveSys()         const { return m_lastSys; }
        bool       enabled()         const { return m_enabled; }

        /// Force the current window to be recorded, e.g. before showing stats.
        void closeAndFlush();

    private:
        Tracker() = default;

        using Clock = std::chrono::steady_clock;

        void closeBucket();
        void resetBucket();
        void refreshContext(bool forceClose);
        void reloadSettings();

        bool m_started = false;
        bool m_enabled = true;

        // settings cache, refreshed once per bucket
        int  m_intervalSec  = 5;
        int  m_flushSec     = 30;
        bool m_trackMenus   = true;

        Clock::time_point m_lastFrame{};
        Clock::time_point m_bucketStart{};
        Clock::time_point m_lastFlush{};
        bool              m_hasLastFrame = false;

        // current window
        uint32_t           m_frames     = 0;
        double             m_frameSumMs = 0.0;
        float              m_minFrameMs = 0.f;
        float              m_maxFrameMs = 0.f;
        std::vector<float> m_frameTimes;

        // live readout window
        uint32_t m_liveFrames  = 0;
        double   m_liveTimeMs  = 0.0;
        float    m_liveFps     = 0.f;
        float    m_liveFrameMs = 0.f;

        // context
        uint8_t   m_context     = 0;
        int32_t   m_levelID     = 0;
        uint8_t   m_flags       = 0;
        uint32_t  m_frameCheck  = 0;
        SysSample m_lastSys{};
    };
}
