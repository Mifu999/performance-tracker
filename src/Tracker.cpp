#include "Tracker.hpp"

#include "Storage.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <functional>

using namespace geode::prelude;
using namespace pt;

namespace {
    /// A frame longer than this is treated as the game being suspended
    /// (alt-tab, long load, breakpoint) rather than a stutter, and voids the
    /// current window instead of dragging its minimum down to nothing.
    constexpr float kStallThresholdMs = 2000.f;

    /// Keep memory bounded on very long windows at very high frame rates.
    constexpr size_t kMaxFrameTimes = 32768;

    /// Local levels have no server ID; derive a stable negative one from the
    /// name so they can still be told apart in the stats.
    int32_t localLevelID(std::string const& name) {
        if (name.empty()) return 0;
        auto h = static_cast<uint32_t>(std::hash<std::string>{}(name) & 0x7FFFFFFFu);
        if (h == 0) h = 1;
        return -static_cast<int32_t>(h);
    }
}

Tracker& Tracker::get() {
    static Tracker inst;
    return inst;
}

void Tracker::reloadSettings() {
    auto mod = Mod::get();
    m_enabled     = mod->getSettingValue<bool>("enabled");
    m_intervalSec = static_cast<int>(mod->getSettingValue<int64_t>("sample-interval"));
    m_flushSec    = static_cast<int>(mod->getSettingValue<int64_t>("flush-interval"));
    m_trackMenus  = mod->getSettingValue<bool>("track-menus");
    m_intervalSec = std::clamp(m_intervalSec, 1, 60);
    m_flushSec    = std::clamp(m_flushSec, 5, 600);
}

void Tracker::onLoad() {
    if (m_started) return;

    this->reloadSettings();

    auto& storage = Storage::get();
    storage.init();

    int retention = static_cast<int>(Mod::get()->getSettingValue<int64_t>("retention-days"));
    if (retention > 0) storage.pruneOld(retention);

    storage.beginSession();
    SystemStats::get().reset();
    SystemStats::get().sample(); // prime the CPU counters

    m_frameTimes.reserve(2048);
    m_started = true;

    log::info(
        "Tracking started (interval {}s, flush {}s, cpu {}, ram {})",
        m_intervalSec, m_flushSec,
        SystemStats::cpuSupported() ? "yes" : "unavailable",
        SystemStats::ramSupported() ? "yes" : "unavailable"
    );
}

void Tracker::resetBucket() {
    m_frames     = 0;
    m_frameSumMs = 0.0;
    m_minFrameMs = 0.f;
    m_maxFrameMs = 0.f;
    m_frameTimes.clear();
}

void Tracker::refreshContext(bool forceClose) {
    auto    ctx     = static_cast<uint8_t>(Context::Menu);
    uint8_t flags   = 0;
    int32_t levelID = 0;

    GJGameLevel* level = nullptr;

    if (auto pl = PlayLayer::get()) {
        ctx   = static_cast<uint8_t>(pl->m_isPaused ? Context::Paused : Context::Playing);
        level = pl->m_level;
        if (pl->m_isPracticeMode) flags |= FlagPractice;
        if (pl->m_isTestMode)     flags |= FlagTestMode;
    }
    else if (auto el = LevelEditorLayer::get()) {
        ctx   = static_cast<uint8_t>(Context::Editor);
        level = el->m_level;
    }

    if (level) {
        std::string name = level->m_levelName.c_str();
        levelID = level->m_levelID.value();
        if (levelID == 0) levelID = localLevelID(name);
        if (levelID != m_levelID && levelID != 0) {
            Storage::get().noteLevel(levelID, name);
        }
    }

    bool changed = (ctx != m_context) || (levelID != m_levelID);

    if (changed && forceClose && m_frames > 1) {
        // Close the window on the OLD context so a sample never mixes two levels
        this->closeBucket();
    }

    m_context = ctx;
    m_levelID = levelID;
    m_flags   = flags;
}

void Tracker::onFrame() {
    if (!m_started) return;

    auto now = Clock::now();

    if (!m_hasLastFrame) {
        m_lastFrame    = now;
        m_bucketStart  = now;
        m_lastFlush    = now;
        m_hasLastFrame = true;
        this->refreshContext(false);
        return;
    }

    float dtMs  = std::chrono::duration<float, std::milli>(now - m_lastFrame).count();
    m_lastFrame = now;

    if (!m_enabled) {
        m_bucketStart = now;
        return;
    }
    if (dtMs <= 0.f) return;

    if (dtMs > kStallThresholdMs) {
        this->resetBucket();
        m_bucketStart = now;
        m_liveFrames  = 0;
        m_liveTimeMs  = 0.0;
        return;
    }

    m_frames += 1;
    m_frameSumMs += dtMs;
    if (m_frames == 1) {
        m_minFrameMs = dtMs;
        m_maxFrameMs = dtMs;
    }
    else {
        m_minFrameMs = std::min(m_minFrameMs, dtMs);
        m_maxFrameMs = std::max(m_maxFrameMs, dtMs);
    }
    if (m_frameTimes.size() < kMaxFrameTimes) m_frameTimes.push_back(dtMs);

    m_liveFrames += 1;
    m_liveTimeMs += dtMs;
    if (m_liveTimeMs >= 400.0) {
        m_liveFps     = static_cast<float>(m_liveFrames * 1000.0 / m_liveTimeMs);
        m_liveFrameMs = static_cast<float>(m_liveTimeMs / m_liveFrames);
        m_liveFrames  = 0;
        m_liveTimeMs  = 0.0;
    }

    if (++m_frameCheck >= 10) {
        m_frameCheck = 0;
        this->refreshContext(true);
    }

    if (std::chrono::duration<double>(now - m_bucketStart).count() >= m_intervalSec) {
        this->closeBucket();
    }

    if (std::chrono::duration<double>(now - m_lastFlush).count() >= m_flushSec) {
        Storage::get().flush();
        m_lastFlush = now;
    }
}

void Tracker::closeBucket() {
    auto   now     = Clock::now();
    double elapsed = std::chrono::duration<double>(now - m_bucketStart).count();

    // The CPU counters are read every window even when the window itself is
    // discarded, so the averaging interval stays consistent.
    m_lastSys = SystemStats::get().sample();

    this->reloadSettings();

    if (m_frames < 2 || elapsed < 0.5) {
        this->resetBucket();
        m_bucketStart = now;
        return;
    }

    bool isMenu = m_context == static_cast<uint8_t>(Context::Menu)
               || m_context == static_cast<uint8_t>(Context::Unknown);

    if (m_enabled && !(isMenu && !m_trackMenus)) {
        Sample s;
        s.time     = static_cast<uint32_t>(std::time(nullptr));
        s.duration = static_cast<uint16_t>(std::clamp<long long>(std::llround(elapsed), 1, 65535));
        s.frames   = static_cast<uint16_t>(std::min<uint32_t>(m_frames, 65535));
        s.fpsAvg   = static_cast<float>(m_frames / elapsed);
        s.fpsMin   = m_maxFrameMs > 0.f ? 1000.f / m_maxFrameMs : 0.f;
        s.fpsMax   = m_minFrameMs > 0.f ? 1000.f / m_minFrameMs : 0.f;

        // 1% low = average frame time of the slowest 1% of frames in the window
        s.fpsLow1 = s.fpsMin;
        if (m_frameTimes.size() >= 20) {
            auto times = m_frameTimes;
            std::sort(times.begin(), times.end(), std::greater<float>());
            size_t count = std::max<size_t>(1, times.size() / 100);
            double sum = 0.0;
            for (size_t i = 0; i < count; ++i) sum += times[i];
            double avgWorst = sum / static_cast<double>(count);
            if (avgWorst > 0.0) s.fpsLow1 = static_cast<float>(1000.0 / avgWorst);
        }

        s.cpuProc = toPermille(m_lastSys.procCpu);
        s.cpuSys  = toPermille(m_lastSys.sysCpu);
        s.ramMB   = m_lastSys.ramMB >= 0
                  ? static_cast<uint16_t>(std::min(m_lastSys.ramMB, 65534))
                  : kNA16;

        if (auto gm = GameManager::sharedState()) {
            float target = gm->m_customFPSTarget;
            if (target > 0.f && target < 65535.f) {
                s.targetFps = static_cast<uint16_t>(std::lround(target));
            }
        }

        s.levelID = m_levelID;
        s.context = m_context;
        s.flags   = m_flags;

        Storage::get().push(s);
    }

    this->resetBucket();
    m_bucketStart = now;
}

void Tracker::closeAndFlush() {
    if (!m_started) return;
    this->closeBucket();
    Storage::get().flush();
    m_lastFlush = Clock::now();
}

void Tracker::onExit() {
    if (!m_started) return;
    this->closeBucket();
    Storage::get().endSession(true);
    Storage::get().flush();
    m_started = false;
    log::info("Tracking stopped cleanly");
}
