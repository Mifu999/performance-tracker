#pragma once

#include "Types.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace pt {

    struct SessionInfo {
        std::time_t start   = 0;
        std::time_t end     = 0;
        bool        clean   = false; ///< false = the game was closed without a clean exit (crash)
        uint32_t    samples = 0;
        double      seconds = 0.0;
        double      fpsWeighted = 0.0;

        float avgFps() const { return seconds > 0.0 ? static_cast<float>(fpsWeighted / seconds) : 0.f; }
    };

    /// Owns everything that touches the disk.
    class Storage {
    public:
        static Storage& get();

        void init();

        std::filesystem::path dataDir() const { return m_dir; }

        /// Buffer a sample. Written on the next flush().
        void push(Sample const& s);
        void flush();

        /// All samples with `from <= time <= to`, including buffered ones.
        std::vector<Sample> query(std::time_t from, std::time_t to);

        /// Local midnight of the oldest day with data, or 0 if there is none.
        std::time_t earliestDay();

        void        noteLevel(int32_t id, std::string name);
        std::string levelName(int32_t id) const;

        std::vector<SessionInfo> const& sessions() const { return m_sessions; }
        void beginSession();
        void addSessionSample(Sample const& s);
        void endSession(bool clean);

        void     pruneOld(int days);
        uint64_t diskUsage() const;

        /// Writes a CSV of the range. Returns an empty path on failure.
        std::filesystem::path exportCsv(std::time_t from, std::time_t to);

    private:
        Storage() = default;

        void loadLevels();
        void saveLevels();
        void loadSessions();
        void saveSessions();

        std::filesystem::path            m_dir;
        std::vector<Sample>              m_pending;
        std::map<int32_t, std::string>   m_levels;
        std::vector<SessionInfo>         m_sessions;
        bool                             m_levelsDirty = false;
        bool                             m_ready       = false;
    };
}
