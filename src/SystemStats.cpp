#include "SystemStats.hpp"

#include <Geode/DefaultInclude.hpp>
#include <algorithm>
#include <cstdio>

#if defined(GEODE_IS_ANDROID)
    #include <unistd.h>
#endif

using namespace pt;

// =====================================================================
// Windows
// =====================================================================
#if defined(GEODE_IS_WINDOWS)

namespace {
    // Layout of PROCESS_MEMORY_COUNTERS as documented by Microsoft. Declared
    // here instead of including <psapi.h> so the mod never has to link psapi.
    struct PTMemCounters {
        DWORD  cb;
        DWORD  PageFaultCount;
        SIZE_T PeakWorkingSetSize;
        SIZE_T WorkingSetSize;
        SIZE_T QuotaPeakPagedPoolUsage;
        SIZE_T QuotaPagedPoolUsage;
        SIZE_T QuotaPeakNonPagedPoolUsage;
        SIZE_T QuotaNonPagedPoolUsage;
        SIZE_T PagefileUsage;
        SIZE_T PeakPagefileUsage;
    };

    using GetProcMemFn = BOOL(WINAPI*)(HANDLE, PTMemCounters*, DWORD);

    // K32GetProcessMemoryInfo is exported by kernel32.dll, resolved lazily so a
    // missing export degrades to "RAM unavailable" instead of a load failure.
    GetProcMemFn getProcMemFn() {
        static GetProcMemFn fn = [] {
            if (HMODULE k32 = GetModuleHandleA("kernel32.dll")) {
                return reinterpret_cast<GetProcMemFn>(
                    reinterpret_cast<void*>(GetProcAddress(k32, "K32GetProcessMemoryInfo"))
                );
            }
            return static_cast<GetProcMemFn>(nullptr);
        }();
        return fn;
    }

    uint64_t ftToU64(FILETIME const& ft) {
        ULARGE_INTEGER u;
        u.LowPart  = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u.QuadPart;
    }
}

bool SystemStats::cpuSupported() { return true; }
bool SystemStats::ramSupported() { return getProcMemFn() != nullptr; }

SystemStats::SystemStats() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    m_cores = std::max<int>(1, static_cast<int>(si.dwNumberOfProcessors));
}

SysSample SystemStats::sample() {
    SysSample out;

    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        uint64_t proc = ftToU64(kernel) + ftToU64(user);

        FILETIME nowFt{};
        GetSystemTimeAsFileTime(&nowFt);
        uint64_t wall = ftToU64(nowFt);

        if (m_hasPrev && wall > m_prevWall && proc >= m_prevProc) {
            double dWall = static_cast<double>(wall - m_prevWall);
            double dProc = static_cast<double>(proc - m_prevProc);
            out.procCpu = static_cast<float>(dProc / (dWall * m_cores) * 100.0);
        }
        m_prevProc = proc;
        m_prevWall = wall;
    }

    FILETIME idleFt{}, kernFt{}, userFt{};
    if (GetSystemTimes(&idleFt, &kernFt, &userFt)) {
        uint64_t idle  = ftToU64(idleFt);
        uint64_t total = ftToU64(kernFt) + ftToU64(userFt); // kernel time includes idle
        if (m_hasPrev && total > m_prevSysTotal && idle >= m_prevSysIdle) {
            double dTotal = static_cast<double>(total - m_prevSysTotal);
            double dIdle  = static_cast<double>(idle - m_prevSysIdle);
            out.sysCpu = static_cast<float>((dTotal - dIdle) / dTotal * 100.0);
        }
        m_prevSysIdle  = idle;
        m_prevSysTotal = total;
    }

    if (auto fn = getProcMemFn()) {
        PTMemCounters pmc{};
        pmc.cb = sizeof(pmc);
        if (fn(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            out.ramMB = static_cast<int>(pmc.WorkingSetSize / (1024ull * 1024ull));
        }
    }

    m_hasPrev = true;
    if (out.procCpu >= 0.f) out.procCpu = std::clamp(out.procCpu, 0.f, 100.f);
    if (out.sysCpu  >= 0.f) out.sysCpu  = std::clamp(out.sysCpu,  0.f, 100.f);
    return out;
}

// =====================================================================
// Android / Linux
// =====================================================================
#elif defined(GEODE_IS_ANDROID)

namespace {
    bool readProcTimes(uint64_t& outTicks) {
        std::FILE* f = std::fopen("/proc/self/stat", "r");
        if (!f) return false;
        char buf[2048] = {};
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        if (n == 0) return false;
        buf[n] = '\0';

        // Field 2 (comm) may contain spaces, so start parsing after the last ')'
        char* p = std::strrchr(buf, ')');
        if (!p) return false;
        p += 2; // skip ") "

        // From there: state(3) ... utime(14) stime(15) -> 11th and 12th tokens
        unsigned long long utime = 0, stime = 0;
        int field = 3;
        char* tok = std::strtok(p, " ");
        while (tok) {
            if (field == 14) utime = std::strtoull(tok, nullptr, 10);
            if (field == 15) { stime = std::strtoull(tok, nullptr, 10); break; }
            ++field;
            tok = std::strtok(nullptr, " ");
        }
        if (field < 15) return false;
        outTicks = utime + stime;
        return true;
    }

    bool readSysTimes(uint64_t& outIdle, uint64_t& outTotal) {
        std::FILE* f = std::fopen("/proc/stat", "r");
        if (!f) return false;
        char buf[512] = {};
        if (!std::fgets(buf, sizeof(buf), f)) { std::fclose(f); return false; }
        std::fclose(f);

        unsigned long long v[10] = {};
        int got = std::sscanf(
            buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
            &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]
        );
        if (got < 4) return false;
        uint64_t total = 0;
        for (int i = 0; i < got; ++i) total += v[i];
        outIdle  = v[3] + (got > 4 ? v[4] : 0); // idle + iowait
        outTotal = total;
        return true;
    }

    int readResidentMB() {
        std::FILE* f = std::fopen("/proc/self/statm", "r");
        if (!f) return -1;
        unsigned long long size = 0, resident = 0;
        int got = std::fscanf(f, "%llu %llu", &size, &resident);
        std::fclose(f);
        if (got < 2) return -1;
        long page = sysconf(_SC_PAGESIZE);
        if (page <= 0) return -1;
        return static_cast<int>((resident * static_cast<unsigned long long>(page)) / (1024ull * 1024ull));
    }
}

bool SystemStats::cpuSupported() { return true; }
bool SystemStats::ramSupported() { return true; }

SystemStats::SystemStats() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    m_cores = static_cast<int>(n > 0 ? n : 1);
}

SysSample SystemStats::sample() {
    SysSample out;

    static const double ticksPerSec = [] {
        long t = sysconf(_SC_CLK_TCK);
        return t > 0 ? static_cast<double>(t) : 100.0;
    }();

    uint64_t procTicks = 0;
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t wallNs = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);

    if (readProcTimes(procTicks)) {
        if (m_hasPrev && wallNs > m_prevWall && procTicks >= m_prevProc) {
            double dWallSec = static_cast<double>(wallNs - m_prevWall) / 1e9;
            double dProcSec = static_cast<double>(procTicks - m_prevProc) / ticksPerSec;
            out.procCpu = static_cast<float>(dProcSec / (dWallSec * m_cores) * 100.0);
        }
        m_prevProc = procTicks;
        m_prevWall = wallNs;
    }

    uint64_t idle = 0, total = 0;
    if (readSysTimes(idle, total)) {
        if (m_hasPrev && total > m_prevSysTotal && idle >= m_prevSysIdle) {
            double dTotal = static_cast<double>(total - m_prevSysTotal);
            double dIdle  = static_cast<double>(idle - m_prevSysIdle);
            out.sysCpu = static_cast<float>((dTotal - dIdle) / dTotal * 100.0);
        }
        m_prevSysIdle  = idle;
        m_prevSysTotal = total;
    }

    out.ramMB = readResidentMB();

    m_hasPrev = true;
    if (out.procCpu >= 0.f) out.procCpu = std::clamp(out.procCpu, 0.f, 100.f);
    if (out.sysCpu  >= 0.f) out.sysCpu  = std::clamp(out.sysCpu,  0.f, 100.f);
    return out;
}

// =====================================================================
// macOS / iOS
// =====================================================================
#else

// Not implemented. Reading CPU/RAM here means Mach task_info(), which I have no
// way to compile or verify for this project, so rather than shipping untested
// code the counters are reported as unavailable and the UI shows "-" for them.
// FPS tracking still works fully on these platforms.

bool SystemStats::cpuSupported() { return false; }
bool SystemStats::ramSupported() { return false; }

SystemStats::SystemStats() {
    m_cores = 1;
}

SysSample SystemStats::sample() {
    m_hasPrev = true;
    return SysSample{};
}

#endif

// =====================================================================
// Shared
// =====================================================================

SystemStats& SystemStats::get() {
    static SystemStats inst;
    return inst;
}

void SystemStats::reset() {
    m_hasPrev      = false;
    m_prevProc     = 0;
    m_prevWall     = 0;
    m_prevSysIdle  = 0;
    m_prevSysTotal = 0;
}
