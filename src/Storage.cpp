#include "Storage.hpp"

#include <Geode/Geode.hpp>

#include <cstring>
#include <fstream>
#include <sstream>

using namespace geode::prelude;
using namespace pt;

namespace {
    constexpr size_t kMaxStoredSessions = 500;

    std::string sanitize(std::string s) {
        for (auto& c : s) {
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        }
        if (s.size() > 96) s.resize(96);
        return s;
    }

    bool writeHeader(std::ofstream& out) {
        out.write(kMagic, 4);
        uint16_t v = kFormatVersion;
        uint16_t r = kRecordSize;
        out.write(reinterpret_cast<char const*>(&v), sizeof(v));
        out.write(reinterpret_cast<char const*>(&r), sizeof(r));
        return out.good();
    }
}

Storage& Storage::get() {
    static Storage inst;
    return inst;
}

void Storage::init() {
    if (m_ready) return;

    m_dir = Mod::get()->getSaveDir() / "perfdata";
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    if (ec) {
        log::error("Could not create data directory {}: {}", m_dir.string(), ec.message());
        return;
    }

    m_ready = true;
    this->loadLevels();
    this->loadSessions();
}

// ---------------------------------------------------------------------
// Samples
// ---------------------------------------------------------------------

void Storage::push(Sample const& s) {
    m_pending.push_back(s);
    this->addSessionSample(s);
}

void Storage::flush() {
    if (!m_ready || m_pending.empty()) return;

    // Samples can straddle midnight, so group by the day each one belongs to.
    std::map<std::string, std::vector<Sample>> byDay;
    for (auto const& s : m_pending) {
        byDay[dayKey(static_cast<std::time_t>(s.time))].push_back(s);
    }

    for (auto const& [day, samples] : byDay) {
        auto path = m_dir / (day + ".bin");

        std::error_code ec;
        bool exists = std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) >= 8;

        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (!out) {
            log::error("Could not open {} for writing", path.string());
            continue;
        }
        if (!exists && !writeHeader(out)) {
            log::error("Could not write header to {}", path.string());
            continue;
        }
        for (auto const& s : samples) {
            out.write(reinterpret_cast<char const*>(&s), sizeof(Sample));
        }
    }

    m_pending.clear();

    if (m_levelsDirty) this->saveLevels();
    this->saveSessions();
}

std::vector<Sample> Storage::query(std::time_t from, std::time_t to) {
    std::vector<Sample> out;
    if (!m_ready || to < from) return out;

    std::time_t cursor = dayStart(from);
    std::time_t last   = dayStart(to);

    while (cursor <= last) {
        auto path = m_dir / (dayKey(cursor) + ".bin");
        std::ifstream in(path, std::ios::binary);
        if (in) {
            char magic[4] = {};
            uint16_t version = 0, recordSize = 0;
            in.read(magic, 4);
            in.read(reinterpret_cast<char*>(&version), sizeof(version));
            in.read(reinterpret_cast<char*>(&recordSize), sizeof(recordSize));

            if (in && std::memcmp(magic, kMagic, 4) == 0 && recordSize == kRecordSize) {
                Sample s{};
                while (in.read(reinterpret_cast<char*>(&s), sizeof(Sample))) {
                    auto t = static_cast<std::time_t>(s.time);
                    if (t >= from && t <= to) out.push_back(s);
                }
            }
            else if (in.gcount() > 0 || in) {
                log::warn("Skipping {}: unknown or corrupted format", path.string());
            }
        }

        std::time_t next = dayOffset(cursor, 1);
        if (next <= cursor) break; // never loop forever on a broken tm conversion
        cursor = next;
    }

    // Not-yet-written samples still belong to the range.
    for (auto const& s : m_pending) {
        auto t = static_cast<std::time_t>(s.time);
        if (t >= from && t <= to) out.push_back(s);
    }

    std::sort(out.begin(), out.end(), [](Sample const& a, Sample const& b) {
        return a.time < b.time;
    });
    return out;
}

std::time_t Storage::earliestDay() {
    if (!m_ready) return 0;

    std::time_t best = 0;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(m_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".bin") continue;

        std::time_t day = 0;
        if (!parseDayKey(entry.path().stem().string(), day)) continue;
        if (best == 0 || day < best) best = day;
    }
    return best;
}

// ---------------------------------------------------------------------
// Level names
// ---------------------------------------------------------------------

void Storage::noteLevel(int32_t id, std::string name) {
    if (id == 0) return;
    name = sanitize(std::move(name));
    if (name.empty()) return;

    auto it = m_levels.find(id);
    if (it == m_levels.end() || it->second != name) {
        m_levels[id]  = name;
        m_levelsDirty = true;
    }
}

std::string Storage::levelName(int32_t id) const {
    if (id == 0) return "-";
    auto it = m_levels.find(id);
    if (it != m_levels.end()) return it->second;
    return fmt::format("Level {}", id);
}

void Storage::loadLevels() {
    std::ifstream in(m_dir / "levels.tsv");
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        try {
            int32_t id = static_cast<int32_t>(std::stol(line.substr(0, tab)));
            m_levels[id] = line.substr(tab + 1);
        }
        catch (...) {
            // ignore malformed lines instead of dropping the whole file
        }
    }
}

void Storage::saveLevels() {
    std::ofstream out(m_dir / "levels.tsv", std::ios::trunc);
    if (!out) return;
    for (auto const& [id, name] : m_levels) {
        out << id << '\t' << name << '\n';
    }
    m_levelsDirty = false;
}

// ---------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------

void Storage::beginSession() {
    SessionInfo s;
    s.start = std::time(nullptr);
    s.end   = s.start;
    s.clean = false;
    m_sessions.push_back(s);

    if (m_sessions.size() > kMaxStoredSessions) {
        m_sessions.erase(m_sessions.begin(), m_sessions.begin() + (m_sessions.size() - kMaxStoredSessions));
    }
    this->saveSessions();
}

void Storage::addSessionSample(Sample const& s) {
    if (m_sessions.empty()) return;
    auto& cur = m_sessions.back();
    cur.samples += 1;
    cur.seconds += s.duration;
    cur.fpsWeighted += static_cast<double>(s.fpsAvg) * s.duration;
    cur.end = static_cast<std::time_t>(s.time);
}

void Storage::endSession(bool clean) {
    if (m_sessions.empty()) return;
    auto& cur = m_sessions.back();
    cur.clean = clean;
    cur.end   = std::time(nullptr);
    this->saveSessions();
}

void Storage::loadSessions() {
    std::ifstream in(m_dir / "sessions.tsv");
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        SessionInfo s;
        long long start = 0, end = 0;
        int clean = 0;
        unsigned long samples = 0;
        double seconds = 0, weighted = 0;
        if (!(ss >> start >> end >> clean >> samples >> seconds >> weighted)) continue;
        s.start       = static_cast<std::time_t>(start);
        s.end         = static_cast<std::time_t>(end);
        s.clean       = clean != 0;
        s.samples     = static_cast<uint32_t>(samples);
        s.seconds     = seconds;
        s.fpsWeighted = weighted;
        m_sessions.push_back(s);
    }
}

void Storage::saveSessions() {
    if (!m_ready) return;
    std::ofstream out(m_dir / "sessions.tsv", std::ios::trunc);
    if (!out) return;
    for (auto const& s : m_sessions) {
        out << static_cast<long long>(s.start) << ' '
            << static_cast<long long>(s.end) << ' '
            << (s.clean ? 1 : 0) << ' '
            << s.samples << ' '
            << s.seconds << ' '
            << s.fpsWeighted << '\n';
    }
}

// ---------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------

void Storage::pruneOld(int days) {
    if (!m_ready || days <= 0) return;

    std::time_t cutoff = dayOffset(std::time(nullptr), -days);
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(m_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".bin") continue;

        std::time_t day = 0;
        if (!parseDayKey(entry.path().stem().string(), day)) continue;
        if (day < cutoff) {
            std::error_code rmEc;
            std::filesystem::remove(entry.path(), rmEc);
            if (!rmEc) log::info("Pruned old performance data: {}", entry.path().filename().string());
        }
    }
}

uint64_t Storage::diskUsage() const {
    if (!m_ready) return 0;
    uint64_t total = 0;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(m_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::error_code sizeEc;
        auto size = entry.file_size(sizeEc);
        if (!sizeEc) total += size;
    }
    return total;
}

std::filesystem::path Storage::exportCsv(std::time_t from, std::time_t to) {
    if (!m_ready) return {};

    auto samples = this->query(from, to);
    if (samples.empty()) return {};

    auto exportDir = m_dir / "exports";
    std::error_code ec;
    std::filesystem::create_directories(exportDir, ec);
    if (ec) return {};

    auto name = fmt::format("perf-{}-to-{}.csv", dayKey(from), dayKey(to));
    auto path = exportDir / name;

    std::ofstream out(path, std::ios::trunc);
    if (!out) return {};

    out << "datetime,unix,duration_s,frames,fps_avg,fps_min,fps_max,fps_low1,"
           "cpu_process_pct,cpu_system_pct,ram_mb,target_fps,context,level_id,level_name,practice,test_mode\n";

    for (auto const& s : samples) {
        auto t = static_cast<std::time_t>(s.time);
        out << formatDateTime(t) << ','
            << s.time << ','
            << s.duration << ','
            << s.frames << ','
            << fmt::format("{:.2f}", s.fpsAvg) << ','
            << fmt::format("{:.2f}", s.fpsMin) << ','
            << fmt::format("{:.2f}", s.fpsMax) << ','
            << fmt::format("{:.2f}", s.fpsLow1) << ',';

        if (hasCpu(s))    out << fmt::format("{:.1f}", cpuPercent(s.cpuProc)); 
        out << ',';
        if (hasSysCpu(s)) out << fmt::format("{:.1f}", cpuPercent(s.cpuSys));
        out << ',';
        if (hasRam(s))    out << s.ramMB;
        out << ','
            << s.targetFps << ','
            << contextName(s.context) << ','
            << s.levelID << ','
            << '"' << this->levelName(s.levelID) << '"' << ','
            << ((s.flags & FlagPractice) ? 1 : 0) << ','
            << ((s.flags & FlagTestMode) ? 1 : 0) << '\n';
    }

    return path;
}
