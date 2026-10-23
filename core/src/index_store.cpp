// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/index_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <filesystem>

namespace clivus {
namespace fs = std::filesystem;

namespace {

constexpr const char* kIndexName = ".clivus-index.sqlite";
// Bumped when the cached columns change: an index written by an older build simply reports a miss and
// the folder is rescanned, so there is no migration path to keep working.
constexpr const char* kSchemaVersion = "2";

bool exec(sqlite3* db, const char* sql)
{
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

// Cheap change detector for the data folder: file count, newest mtime, total bytes. It is a walk, not
// a DICOM parse, so it costs ~2 ms on a 50-slice series — versus ~12 ms to re-read every header.
// shortcut: a file replaced with identical size and mtime reads as unchanged, upgrade to per-file
// stamps if anyone ever edits series in place.
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
        if (name.rfind(kIndexName, 0) == 0) continue;   // our own index is not data
        ++count;
        const auto stamp = fs::last_write_time(it->path(), e);
        if (!e) newest = std::max(newest, (long long)stamp.time_since_epoch().count());
        const auto size = fs::file_size(it->path(), e);
        if (!e) bytes += (long long)size;
    }
    return std::to_string(count) + ":" + std::to_string(newest) + ":" + std::to_string(bytes);
}

void bindText(sqlite3_stmt* st, int index, const std::string& value)
{
    sqlite3_bind_text(st, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

IndexStore::~IndexStore()
{
    close();
}

bool IndexStore::open(const std::string& dataFolder)
{
    close();
    std::error_code ec;
    fs::create_directories(dataFolder, ec);   // harmless when it exists
    m_path = (fs::path(dataFolder) / kIndexName).string();
    if (sqlite3_open_v2(m_path.c_str(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        close();
        return false;
    }
    const char* schema =
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);"
        "CREATE TABLE IF NOT EXISTS series(series_uid TEXT PRIMARY KEY, ordinal INTEGER, study_uid TEXT,"
        " modality TEXT, patient_name TEXT, study_description TEXT, series_description TEXT,"
        " width INTEGER, height INTEGER);"
        "CREATE TABLE IF NOT EXISTS series_file(series_uid TEXT, ordinal INTEGER, path TEXT,"
        " frames INTEGER);"
        "CREATE INDEX IF NOT EXISTS series_file_uid ON series_file(series_uid, ordinal);";
    if (!exec(m_db, schema)) {
        close();
        return false;
    }
    m_fingerprint = fingerprintOf(dataFolder);
    return true;
}

std::vector<SeriesInfo> IndexStore::load()
{
    std::vector<SeriesInfo> out;
    if (!m_db) return out;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT key, value FROM meta", -1, &st, nullptr) != SQLITE_OK) return out;
    std::string version, fingerprint;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const std::string key = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        const auto* value = sqlite3_column_text(st, 1);
        const std::string text = value ? reinterpret_cast<const char*>(value) : "";
        if (key == "version") version = text;
        if (key == "fingerprint") fingerprint = text;
    }
    sqlite3_finalize(st);
    // Wrong version or a folder that changed since the scan: report a miss, and the caller rescans.
    if (version != kSchemaVersion || fingerprint != m_fingerprint) return out;

    const char* sql =
        "SELECT s.series_uid, s.study_uid, s.modality, s.patient_name, s.study_description,"
        " s.series_description, s.width, s.height, f.path, f.frames"
        " FROM series s LEFT JOIN series_file f ON f.series_uid = s.series_uid"
        " ORDER BY s.ordinal, f.ordinal;";
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
            s.patientName = text(3);
            s.studyDescription = text(4);
            s.seriesDescription = text(5);
            s.width = sqlite3_column_int(st, 6);
            s.height = sqlite3_column_int(st, 7);
            out.push_back(std::move(s));
        }
        const auto* path = sqlite3_column_text(st, 8);
        if (path) {
            out.back().files.emplace_back(reinterpret_cast<const char*>(path));
            // A row from before frames were cached reads as NULL here, which totalFrames() treats as
            // one frame per object — but that cannot happen for a version-2 index.
            const int frames = sqlite3_column_int(st, 9);
            out.back().frameCounts.push_back(frames > 0 ? frames : 1);
        }
    }
    sqlite3_finalize(st);
    return out;
}

void IndexStore::save(const std::vector<SeriesInfo>& series)
{
    if (!m_db) return;
    exec(m_db, "BEGIN");
    exec(m_db, "DELETE FROM series; DELETE FROM series_file;");

    sqlite3_stmt* insertSeries = nullptr;
    sqlite3_stmt* insertFile = nullptr;
    const bool prepared =
        sqlite3_prepare_v2(m_db,
                           "INSERT INTO series VALUES(?,?,?,?,?,?,?,?,?)", -1, &insertSeries,
                           nullptr) == SQLITE_OK &&
        sqlite3_prepare_v2(m_db, "INSERT INTO series_file VALUES(?,?,?,?)", -1, &insertFile,
                           nullptr) == SQLITE_OK;
    if (prepared) {
        for (size_t i = 0; i < series.size(); ++i) {
            const SeriesInfo& s = series[i];
            bindText(insertSeries, 1, s.seriesUid);
            sqlite3_bind_int(insertSeries, 2, int(i));
            bindText(insertSeries, 3, s.studyUid);
            bindText(insertSeries, 4, s.modality);
            bindText(insertSeries, 5, s.patientName);
            bindText(insertSeries, 6, s.studyDescription);
            bindText(insertSeries, 7, s.seriesDescription);
            sqlite3_bind_int(insertSeries, 8, s.width);
            sqlite3_bind_int(insertSeries, 9, s.height);
            sqlite3_step(insertSeries);
            sqlite3_reset(insertSeries);

            for (size_t f = 0; f < s.files.size(); ++f) {
                bindText(insertFile, 1, s.seriesUid);
                sqlite3_bind_int(insertFile, 2, int(f));
                bindText(insertFile, 3, s.files[f]);
                const int frames = f < s.frameCounts.size() ? s.frameCounts[f] : 1;
                sqlite3_bind_int(insertFile, 4, frames > 0 ? frames : 1);
                sqlite3_step(insertFile);
                sqlite3_reset(insertFile);
            }
        }
    }
    sqlite3_finalize(insertSeries);
    sqlite3_finalize(insertFile);
    exec(m_db, "COMMIT");

    if (!prepared) return;
    sqlite3_stmt* insertMeta = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO meta VALUES(?,?)", -1, &insertMeta, nullptr) !=
        SQLITE_OK)
        return;
    for (const auto& [key, value] :
         {std::pair<std::string, std::string>{"version", kSchemaVersion},
          {"fingerprint", m_fingerprint}}) {
        bindText(insertMeta, 1, key);
        bindText(insertMeta, 2, value);
        sqlite3_step(insertMeta);
        sqlite3_reset(insertMeta);
    }
    sqlite3_finalize(insertMeta);
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
    store.save(series);   // no-op when the folder is not writable
    return series;
}

}  // namespace clivus
