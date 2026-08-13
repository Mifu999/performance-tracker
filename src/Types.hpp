#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <string>
#include <algorithm>

namespace pt {

    // ---------------------------------------------------------------------
    // On-disk format
    // ---------------------------------------------------------------------
    // One file per LOCAL day: <save dir>/perfdata/YYYY-MM-DD.bin
    //
    //   offset 0 : 'M' 'P' 'F' 'L'      (magic)
    //   offset 4 : uint16 formatVersion
    //   offset 6 : uint16 recordSize
    //   offset 8 : Sample[]             (append only, little endian)
    //
    // Timestamps are stored as UTC unix seconds; the file a sample lands in
    // is chosen from the LOCAL date so "today" always matches the player's day.
    // ---------------------------------------------------------------------

    inline constexpr char     kMagic[4]      = { 'M', 'P', 'F', 'L' };
    inline constexpr uint16_t kFormatVersion = 1;
    inline constexpr uint16_t kRecordSize    = 40;
    inline constexpr uint16_t kNA16          = 0xFFFF;

    enum class Context : uint8_t {
        Unknown = 0,
        Menu    = 1,
        Editor  = 2,
        Playing = 3,
        Paused  = 4,
    };
    inline constexpr size_t kContextCount = 5;

    enum SampleFlags : uint8_t {
        FlagPractice = 1 << 0,
        FlagTestMode = 1 << 1,
    };

#pragma pack(push, 1)
    struct Sample {
        uint32_t time      = 0;      // UTC unix seconds, END of the covered window
        uint16_t duration  = 0;      // seconds covered by this sample
        uint16_t frames    = 0;      // frames rendered during the window (capped)
        float    fpsAvg    = 0.f;    // frames / duration
        float    fpsMin    = 0.f;    // 1000 / worst frame time
        float    fpsMax    = 0.f;    // 1000 / best frame time
        float    fpsLow1   = 0.f;    // 1% low within this window
        uint16_t cpuProc   = kNA16;  // process CPU, permille of total machine
        uint16_t cpuSys    = kNA16;  // whole system CPU, permille
        uint16_t ramMB     = kNA16;  // process working set, MB
        uint16_t targetFps = 0;      // GD's configured FPS target, 0 = unknown
        int32_t  levelID   = 0;      // 0 = not in a level
        uint8_t  context   = 0;      // Context
        uint8_t  flags     = 0;      // SampleFlags
        uint16_t reserved  = 0;
    };
#pragma pack(pop)

    static_assert(sizeof(Sample) == kRecordSize, "Sample must stay 40 bytes: the on-disk format depends on it");

    inline bool hasCpu(Sample const& s) { return s.cpuProc != kNA16; }
    inline bool hasSysCpu(Sample const& s) { return s.cpuSys != kNA16; }
    inline bool hasRam(Sample const& s) { return s.ramMB != kNA16; }

    inline float cpuPercent(uint16_t permille) { return permille == kNA16 ? -1.f : permille / 10.f; }
    inline uint16_t toPermille(float percent) {
        if (percent < 0.f) return kNA16;
        int v = static_cast<int>(std::lround(percent * 10.f));
        return static_cast<uint16_t>(std::clamp(v, 0, 65534));
    }

    inline const char* contextName(uint8_t c) {
        switch (static_cast<Context>(c)) {
            case Context::Menu:    return "Menus";
            case Context::Editor:  return "Editor";
            case Context::Playing: return "Playing";
            case Context::Paused:  return "Paused";
            default:               return "Other";
        }
    }

    // ---------------------------------------------------------------------
    // Time helpers (everything user-facing is LOCAL time)
    // ---------------------------------------------------------------------

    inline std::tm localTime(std::time_t t) {
        std::tm out{};
#ifdef _WIN32
        localtime_s(&out, &t);
#else
        localtime_r(&t, &out);
#endif
        return out;
    }

    inline std::time_t fromLocalTm(std::tm tm) {
        tm.tm_isdst = -1;
        return std::mktime(&tm);
    }

    /// Local midnight of the day containing `t`.
    inline std::time_t dayStart(std::time_t t) {
        std::tm tm = localTime(t);
        tm.tm_hour = 0;
        tm.tm_min  = 0;
        tm.tm_sec  = 0;
        return fromLocalTm(tm);
    }

    /// Local midnight `days` days after the day containing `t` (DST safe).
    inline std::time_t dayOffset(std::time_t t, int days) {
        std::tm tm = localTime(t);
        tm.tm_hour = 12; // avoid DST edges while adding days
        tm.tm_min  = 0;
        tm.tm_sec  = 0;
        tm.tm_mday += days;
        return dayStart(fromLocalTm(tm));
    }

    inline std::string dayKey(std::time_t t) {
        std::tm tm = localTime(t);
        char buf[16] = {};
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        return buf;
    }

    /// Accepts "YYYY-MM-DD" and tolerates unpadded parts ("2026-8-1").
    inline bool parseDayKey(std::string const& s, std::time_t& out) {
        int    parts[3] = { 0, 0, 0 };
        int    idx      = 0;
        size_t digits   = 0;

        for (char c : s) {
            if (c >= '0' && c <= '9') {
                if (digits >= 4) return false;
                parts[idx] = parts[idx] * 10 + (c - '0');
                ++digits;
            }
            else if (c == '-' || c == '/') {
                if (digits == 0 || idx >= 2) return false;
                ++idx;
                digits = 0;
            }
            else {
                return false;
            }
        }
        if (idx != 2 || digits == 0) return false;

        int y = parts[0], m = parts[1], d = parts[2];
        if (y < 1970 || y > 3000 || m < 1 || m > 12 || d < 1 || d > 31) return false;

        std::tm tm{};
        tm.tm_year = y - 1900;
        tm.tm_mon  = m - 1;
        tm.tm_mday = d;
        tm.tm_hour = 0;
        std::time_t r = fromLocalTm(tm);
        if (r == static_cast<std::time_t>(-1)) return false;
        out = r;
        return true;
    }

    inline std::string formatClock(std::time_t t) {
        std::tm tm = localTime(t);
        char buf[8] = {};
        std::strftime(buf, sizeof(buf), "%H:%M", &tm);
        return buf;
    }

    inline std::string formatDateShort(std::time_t t) {
        std::tm tm = localTime(t);
        char buf[16] = {};
        std::strftime(buf, sizeof(buf), "%d/%m", &tm);
        return buf;
    }

    inline std::string formatDateTime(std::time_t t) {
        std::tm tm = localTime(t);
        char buf[32] = {};
        std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &tm);
        return buf;
    }

    /// "3h 12m", "12m 40s", "40s"
    inline std::string formatDuration(double seconds) {
        if (seconds < 0) seconds = 0;
        long long total = static_cast<long long>(seconds);
        long long h = total / 3600;
        long long m = (total % 3600) / 60;
        long long s = total % 60;
        char buf[48] = {};
        if (h > 0)      std::snprintf(buf, sizeof(buf), "%lldh %02lldm", h, m);
        else if (m > 0) std::snprintf(buf, sizeof(buf), "%lldm %02llds", m, s);
        else            std::snprintf(buf, sizeof(buf), "%llds", s);
        return buf;
    }

    inline std::string formatBytes(uint64_t bytes) {
        char buf[32] = {};
        if (bytes >= 1024ull * 1024ull)  std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024ull)       std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
        else                             std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
        return buf;
    }

    inline std::string fmtFloat(float v, int decimals = 1) {
        if (v < 0.f) return "-";
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        return buf;
    }
}
