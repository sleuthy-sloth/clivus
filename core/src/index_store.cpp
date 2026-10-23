// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/index_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
// windows.h defines min/max as macros unless this comes first, which breaks std::max below with
// "illegal token on right side of '::'" (MSVC only — the Linux build never sees it).
#define NOMINMAX
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace clivus {
namespace fs = std::filesystem;

namespace {

constexpr const char* kIndexName = ".clivus-index.sqlite";   // legacy, in the data folder
constexpr const char* kSchemaVersion = IndexStore::kSchemaVersion;

// ---------------------------------------------------------------------------------------------
// Cache policy (roadmap Phase 1)
//
// Location: an application cache directory, never the DICOM folder. The old index lived beside the
// study and therefore travelled with it — copying a folder to a colleague copied its metadata too.
// Resolution order, first hit wins:
//    1. $CLIVUS_CACHE_DIR          — set by the GUI from QStandardPaths::CacheLocation
//    2. %LOCALAPPDATA%\Clivus\index                        (Windows)
//    3. ~/Library/Caches/Clivus/index                      (macOS)
//    4. $XDG_CACHE_HOME/clivus/index or ~/.cache/clivus/index   (POSIX)
//
// Naming: an FNV-1a 64 hash of the canonical source path. The filename does not spell out where the
// study lives. This is NOT anonymity: the input space of folder paths is small enough to brute
// force, so the hash only keeps the path out of casual file listings. (Stated plainly, because the
// alternative — implying the cache is anonymous — would be a false privacy claim.)
//
// Contents: series/study UIDs, modality, study/series descriptions, rows/columns, file paths,
// per-file frame counts, per-slice geometry, and the folder fingerprint. Patient names and patient
// identifiers are NOT stored. The GUI shows names from the session's in-memory scan; after a
// restart they come back with a rescan, not from yesterday's cache.
//
// Failures: any cache problem degrades to an uncached scan. The cache never owns data the viewer
// needs, so it must never be able to take the viewer down with it.
// ---------------------------------------------------------------------------------------------

std::string envOr(const char* name)
{
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::string homeDir()
{
#if defined(_WIN32)
    return envOr("USERPROFILE");
#else
    std::string home = envOr("HOME");
    if (!home.empty()) return home;
    if (const passwd* pw = ::getpwuid(::getuid())) return pw->pw_dir ? pw->pw_dir : std::string();
    return {};
#endif
}

// The cache directory, or empty when none can be determined (callers then run uncached).
std::string cacheDir()
{
    if (const std::string explicitDir = envOr("CLIVUS_CACHE_DIR"); !explicitDir.empty())
        return explicitDir;
#if defined(_WIN32)
    const std::string local = envOr("LOCALAPPDATA");
    return local.empty() ? std::string() : local + "\\Clivus\\index";
#elif defined(__APPLE__)
    const std::string home = homeDir();
    return home.empty() ? std::string() : home + "/Library/Caches/Clivus/index";
#else
    if (const std::string xdg = envOr("XDG_CACHE_HOME"); !xdg.empty()) return xdg + "/clivus/index";
    const std::string home = homeDir();
    return home.empty() ? std::string() : home + "/.cache/clivus/index";
#endif
}

// FNV-1a 64 over the path bytes. Stable across runs for the same text; not a security boundary.
std::string pathHash(const std::string& text)
{
    unsigned long long hash = 1469598103934665603ull;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", hash);
    return std::string(buffer);
}

std::string canonicalPath(const std::string& dir)
{
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(dir, ec);
    return ec ? dir : canonical.string();
}

bool exec(sqlite3* db, const char* sql)
{
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

// Removes the legacy in-folder index this build no longer writes. It carried patient names, so it
// is deleted on sight rather than left behind to be copied around with the study. Failure (read-only
// media, permissions) is not an error: the viewer keeps working.
void removeLegacyIndex(const std::string& dataFolder)
{
    std::error_code ec;
    fs::remove(fs::path(dataFolder) / kIndexName, ec);
}

// Cheap change detector for the data folder: file count, newest mtime, total bytes. It is a walk,
// not a DICOM parse, so it costs ~2 ms on a 50-slice series — versus ~12 ms to re-read every header.
// KNOWN LIMITATION (documented in the manual): a file replaced by one with identical size and mtime
// reads as unchanged. Content stamps would cost a full read of every file on every scan, which is
// exactly the work the cache exists to avoid.
std::string fingerprintOf(const std::string& dir)
{
    size_t count = 0;
    long long newest = 0;
    long long bytes = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e;
        if (!it->is_regular_file(e)) continue;
        const std::string name = it->path().filename().string();
        if (name.rfind(kIndexName, 0) == 0) continue;   // our own (legacy) index is not data
        ++count;
        const auto stamp = fs::last_write_time(it->path(), e);
        if (!e) newest = (std::max)(newest, (long long)stamp.time_since_epoch().count());
        const auto size = fs::file_size(it->path(), e);
        if (!e) bytes += (long long)size;
    }
    return std::to_string(count) + ":" + std::to_string(newest) + ":" + std::to_string(bytes);
}

bool bindText(sqlite3_stmt* st, int index, const std::string& value)
{
    return sqlite3_bind_text(st, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

// One meta row, or empty.
std::string readMeta(sqlite3* db, const char* key)
{
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key = ?", -1, &st, nullptr) != SQLITE_OK)
        return {};
    bindText(st, 1, key);
    std::string value;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const auto* text = sqlite3_column_text(st, 0);
        if (text) value = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(st);
    return value;
}

}  // namespace

std::string IndexStore::legacyPathFor(const std::string& dataFolder)
{
    return (fs::path(dataFolder) / kIndexName).string();
}

std::string IndexStore::cachePathFor(const std::string& dataFolder)
{
    const std::string dir = cacheDir();
    if (dir.empty()) return {};
    return (fs::path(dir) / (pathHash(canonicalPath(dataFolder)) + ".sqlite")).string();
}

IndexStore::~IndexStore()
{
    close();
}

bool IndexStore::openDatabase(const std::string& path)
{
    if (sqlite3_open_v2(path.c_str(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        close();
        return false;
    }
    // The cache is a convenience: bound how long a broken or held file can block the UI thread.
    sqlite3_busy_timeout(m_db, 1500);
    return ensureSchema();
}

bool IndexStore::ensureSchema()
{
    const char* schema =
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);"
        // No patient_name / patient identifier column: see the cache policy note above.
        "CREATE TABLE IF NOT EXISTS series(series_uid TEXT PRIMARY KEY, ordinal INTEGER,"
        " study_uid TEXT, modality TEXT, study_description TEXT, series_description TEXT,"
        " width INTEGER, height INTEGER);"
        "CREATE TABLE IF NOT EXISTS series_file(series_uid TEXT, ordinal INTEGER, path TEXT,"
        " frames INTEGER,"
        " originX REAL, originY REAL, originZ REAL,"
        " orientRowX REAL, orientRowY REAL, orientRowZ REAL,"
        " orientColX REAL, orientColY REAL, orientColZ REAL,"
        " sliceLocation REAL, instanceNumber INTEGER, hasGeometry INTEGER);"
        "CREATE INDEX IF NOT EXISTS series_file_uid ON series_file(series_uid, ordinal);";
    if (!exec(m_db, schema)) return false;

    // The version check happens at OPEN, not at load: a database written by an older build is
    // dropped here, so rows holding fields the new schema forbids (such as patient names) do not
    // sit on disk waiting for a scan to notice them.
    const std::string version = readMeta(m_db, "version");
    if (!version.empty() && version != kSchemaVersion) {
        if (!exec(m_db, "DROP TABLE IF EXISTS series_file; DROP TABLE IF EXISTS series;"
                        " DELETE FROM meta;"))
            return false;
    }
    return true;
}

bool IndexStore::open(const std::string& dataFolder)
{
    close();
    m_path = cachePathFor(dataFolder);
    if (m_path.empty()) return false;   // no cache dir on this system: run uncached

    std::error_code ec;
    fs::create_directories(fs::path(m_path).parent_path(), ec);   // openDatabase reports failures

    // A legacy in-folder index carries patient names; this build never reads it, and removes it.
    removeLegacyIndex(dataFolder);

    m_fingerprint = fingerprintOf(dataFolder);

    if (openDatabase(m_path)) return true;

    // Recovery: a corrupt or half-written database must not take the viewer down. Delete it, retry
    // once; if it still cannot be used, leave the cache closed and let the caller scan uncached.
    close();
    fs::remove(m_path, ec);
    fs::remove(m_path + "-wal", ec);
    fs::remove(m_path + "-journal", ec);
    if (openDatabase(m_path)) return true;
    close();
    return false;
}

std::vector<SeriesInfo> IndexStore::load()
{
    std::vector<SeriesInfo> out;
    if (!m_db) return out;

    const std::string version = readMeta(m_db, "version");
    const std::string fingerprint = readMeta(m_db, "fingerprint");
    // Wrong version or a folder that changed since the scan: a MISS, so the caller rescans. Never a
    // silently successful empty study.
    if (version != kSchemaVersion || fingerprint != m_fingerprint) return out;

    const char* sql =
        "SELECT s.series_uid, s.study_uid, s.modality, s.study_description,"
        " s.series_description, s.width, s.height, f.path, f.frames,"
        " f.originX, f.originY, f.originZ,"
        " f.orientRowX, f.orientRowY, f.orientRowZ,"
        " f.orientColX, f.orientColY, f.orientColZ,"
        " f.sliceLocation, f.instanceNumber, f.hasGeometry"
        " FROM series s LEFT JOIN series_file f ON f.series_uid = s.series_uid"
        " ORDER BY s.ordinal, f.ordinal;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    const auto text = [&](int col) {
        const auto* v = sqlite3_column_text(st, col);
        return std::string(v ? reinterpret_cast<const char*>(v) : "");
    };
    while (sqlite3_step(st) == SQLITE_ROW) {
        const std::string uid = text(0);
        if (out.empty() || out.back().seriesUid != uid) {
            SeriesInfo s;
            s.seriesUid = uid;
            s.studyUid = text(1);
            s.modality = text(2);
            // patientName stays empty: it is not cached (see the cache policy note).
            s.studyDescription = text(3);
            s.seriesDescription = text(4);
            s.width = sqlite3_column_int(st, 5);
            s.height = sqlite3_column_int(st, 6);
            out.push_back(std::move(s));
        }
        const auto* path = sqlite3_column_text(st, 7);
        if (path) {
            out.back().files.emplace_back(reinterpret_cast<const char*>(path));
            const int frames = sqlite3_column_int(st, 8);
            out.back().frameCounts.push_back(frames > 0 ? frames : 1);

            SeriesInfo::SliceGeometry geom;
            geom.originX = sqlite3_column_double(st, 9);
            geom.originY = sqlite3_column_double(st, 10);
            geom.originZ = sqlite3_column_double(st, 11);
            geom.orientRowX = sqlite3_column_double(st, 12);
            geom.orientRowY = sqlite3_column_double(st, 13);
            geom.orientRowZ = sqlite3_column_double(st, 14);
            geom.orientColX = sqlite3_column_double(st, 15);
            geom.orientColY = sqlite3_column_double(st, 16);
            geom.orientColZ = sqlite3_column_double(st, 17);
            geom.sliceLocation = sqlite3_column_double(st, 18);
            geom.instanceNumber = sqlite3_column_int64(st, 19);
            geom.hasGeometry = sqlite3_column_int(st, 20) != 0;
            out.back().sliceGeometry.push_back(geom);
        }
    }
    sqlite3_finalize(st);
    return out;
}

bool IndexStore::save(const std::vector<SeriesInfo>& series)
{
    if (!m_db) return false;
    if (!exec(m_db, "BEGIN IMMEDIATE")) return false;

    bool ok = exec(m_db, "DELETE FROM series; DELETE FROM series_file;");

    sqlite3_stmt* insertSeries = nullptr;
    sqlite3_stmt* insertFile = nullptr;
    ok = ok && sqlite3_prepare_v2(m_db,
                                  "INSERT INTO series"
                                  " (series_uid, ordinal, study_uid, modality, study_description,"
                                  "  series_description, width, height) VALUES(?,?,?,?,?,?,?,?)",
                                  -1, &insertSeries, nullptr) == SQLITE_OK;
    ok = ok && sqlite3_prepare_v2(
                   m_db,
                   "INSERT INTO series_file (series_uid, ordinal, path, frames, originX, originY,"
                   " originZ, orientRowX, orientRowY, orientRowZ, orientColX, orientColY,"
                   " orientColZ, sliceLocation, instanceNumber, hasGeometry)"
                   " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                   -1, &insertFile, nullptr) == SQLITE_OK;

    for (size_t i = 0; ok && i < series.size(); ++i) {
        const SeriesInfo& s = series[i];
        ok = bindText(insertSeries, 1, s.seriesUid) &&
             sqlite3_bind_int(insertSeries, 2, int(i)) == SQLITE_OK &&
             bindText(insertSeries, 3, s.studyUid) &&
             bindText(insertSeries, 4, s.modality) &&
             bindText(insertSeries, 5, s.studyDescription) &&
             bindText(insertSeries, 6, s.seriesDescription) &&
             sqlite3_bind_int(insertSeries, 7, s.width) == SQLITE_OK &&
             sqlite3_bind_int(insertSeries, 8, s.height) == SQLITE_OK &&
             sqlite3_step(insertSeries) == SQLITE_DONE;
        sqlite3_reset(insertSeries);

        for (size_t f = 0; ok && f < s.files.size(); ++f) {
            const int frames = f < s.frameCounts.size() ? s.frameCounts[f] : 1;
            const SeriesInfo::SliceGeometry geom =
                (f < s.sliceGeometry.size()) ? s.sliceGeometry[f] : SeriesInfo::SliceGeometry{};
            ok = bindText(insertFile, 1, s.seriesUid) &&
                 sqlite3_bind_int(insertFile, 2, int(f)) == SQLITE_OK &&
                 bindText(insertFile, 3, s.files[f]) &&
                 sqlite3_bind_int(insertFile, 4, frames > 0 ? frames : 1) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 5, geom.originX) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 6, geom.originY) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 7, geom.originZ) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 8, geom.orientRowX) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 9, geom.orientRowY) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 10, geom.orientRowZ) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 11, geom.orientColX) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 12, geom.orientColY) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 13, geom.orientColZ) == SQLITE_OK &&
                 sqlite3_bind_double(insertFile, 14, geom.sliceLocation) == SQLITE_OK &&
                 sqlite3_bind_int64(insertFile, 15, geom.instanceNumber) == SQLITE_OK &&
                 sqlite3_bind_int(insertFile, 16, geom.hasGeometry ? 1 : 0) == SQLITE_OK &&
                 sqlite3_step(insertFile) == SQLITE_DONE;
            // Unconditional reset: a finished statement must leave its done state before the next
            // bind, or later binds are rejected and the step replays stale values (this is how one
            // file's path once ended up duplicated under a whole series).
            sqlite3_reset(insertFile);
        }
    }

    sqlite3_finalize(insertSeries);
    sqlite3_finalize(insertFile);

    // Metadata goes in the same transaction as the rows, so a cache is never "half current".
    sqlite3_stmt* insertMeta = nullptr;
    if (ok)
        ok = sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO meta VALUES(?,?)", -1, &insertMeta,
                                nullptr) == SQLITE_OK;
    if (ok) {
        for (const auto& [key, value] :
             {std::pair<std::string, std::string>{"version", kSchemaVersion},
              {"fingerprint", m_fingerprint}}) {
            ok = bindText(insertMeta, 1, key) && bindText(insertMeta, 2, value) &&
                 sqlite3_step(insertMeta) == SQLITE_DONE;
            sqlite3_reset(insertMeta);
            if (!ok) break;
        }
    }
    sqlite3_finalize(insertMeta);

    if (!ok) {
        exec(m_db, "ROLLBACK");   // never leave a partial index behind
        return false;
    }
    if (!exec(m_db, "COMMIT")) {
        exec(m_db, "ROLLBACK");
        return false;
    }
    return true;
}

void IndexStore::clearCache(const std::string& dataFolder)
{
    std::error_code ec;
    const std::string path = cachePathFor(dataFolder);
    if (!path.empty()) {
        fs::remove(path, ec);
        fs::remove(path + "-wal", ec);
        fs::remove(path + "-journal", ec);
    }
    fs::remove(legacyPathFor(dataFolder), ec);
}

void IndexStore::close()
{
    if (m_db) sqlite3_close(m_db);
    m_db = nullptr;
}

std::vector<SeriesInfo> scanDirectoryCached(const std::string& dir, ScanControl* control)
{
    IndexStore store;
    if (store.open(dir)) {
        auto cached = store.load();
        if (!cached.empty()) return cached;
    }
    auto series = scanDirectory(dir, control);
    // A cancelled scan returns an empty list. Saving it would write the folder's fingerprint next to
    // an empty series table, which every later load() reads as a miss anyway — so skip the write and
    // leave the index describing whatever it described before.
    if (control && control->cancel.load(std::memory_order_relaxed)) return series;
    store.save(series);   // best effort: a failed write only costs a rescan next time
    return series;
}

}  // namespace clivus
