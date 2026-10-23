// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "clivus/series_index.h"

#include <string>
#include <vector>

struct sqlite3;

namespace clivus {

// Where the index cache lives and what it is allowed to remember. See "Cache policy" in
// core/src/index_store.cpp for the full account — the short version: an application cache directory
// rather than the DICOM folder, no patient names or identifiers, and a folder fingerprint for
// invalidation.
class IndexStore {
public:
    static constexpr const char* kSchemaVersion = "4";

    ~IndexStore();
    IndexStore() = default;
    IndexStore(const IndexStore&) = delete;
    IndexStore& operator=(const IndexStore&) = delete;

    // Opens (creating if needed) the cache for this data folder. Returns false when the cache cannot
    // be used at all (unwritable location, unrecoverable corruption): callers then simply scan, and
    // the viewer keeps working uncached.
    bool open(const std::string& dataFolder);

    // True once open() succeeded and the cache is usable.
    bool available() const { return m_db != nullptr; }

    // Cached series for the folder as it is now; empty when there is no usable cache. A failed read
    // is a miss (the folder gets rescanned), never a successful empty study.
    std::vector<SeriesInfo> load();

    // Replaces the cached series and fingerprint inside one transaction. False when anything failed;
    // a failed save leaves the previous cache contents intact rather than a half-written table.
    bool save(const std::vector<SeriesInfo>& series);

    void close();

    // Removes the cache for this folder (and any legacy in-folder index this build replaced).
    // Safe to call when no cache exists. Returns false when a file could not be removed — on
    // Windows an open database handle blocks deletion, so a caller that finds this false should
    // say so rather than claim the cache was cleared.
    static bool clearCache(const std::string& dataFolder);

    // The cache file path a folder maps to. Exposed for tests and for the "where is it" answer;
    // empty when no cache directory can be determined.
    static std::string cachePathFor(const std::string& dataFolder);

    // The legacy in-folder index this build no longer writes: <folder>/.clivus-index.sqlite.
    static std::string legacyPathFor(const std::string& dataFolder);

private:
    bool openDatabase(const std::string& path);
    bool ensureSchema();

    std::string m_path;
    std::string m_fingerprint;
    sqlite3* m_db = nullptr;
};

// scanDirectory(), but through the on-disk index: a second start on an unchanged folder parses no
// DICOM headers at all.
std::vector<SeriesInfo> scanDirectoryCached(const std::string& dir, ScanControl* control = nullptr);

}  // namespace clivus
