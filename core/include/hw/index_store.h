#pragma once

#include "hw/series_index.h"

#include <string>
#include <vector>

struct sqlite3;

namespace hw {

// On-disk series index for one data folder: <folder>/.horoswin-index.sqlite.
//
// A rescan of a folder costs one DCMTK header parse per file (~0.2 ms each on a real series), which is
// fine once but wasteful on every start. The index is keyed on a fingerprint of the folder contents, so
// a changed folder simply misses the cache instead of showing stale series — no invalidation logic.
class IndexStore {
public:
    ~IndexStore();
    IndexStore() = default;
    IndexStore(const IndexStore&) = delete;
    IndexStore& operator=(const IndexStore&) = delete;

    // Opens (creating if needed) the index next to the data. False when the folder is not writable or
    // the file is not a usable index: callers then just scan.
    bool open(const std::string& dataFolder);

    // Cached series for the folder as it is now; empty when there is no usable cache.
    std::vector<SeriesInfo> load();

    // Replaces the cached series and the stored fingerprint.
    void save(const std::vector<SeriesInfo>& series);

    void close();

private:
    std::string m_path;
    std::string m_fingerprint;
    sqlite3* m_db = nullptr;
};

// scanDirectory(), but through the on-disk index: a second start on an unchanged folder parses no
// DICOM headers at all.
std::vector<SeriesInfo> scanDirectoryCached(const std::string& dir);

}  // namespace hw
