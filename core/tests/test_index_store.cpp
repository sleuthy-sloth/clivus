// SPDX-License-Identifier: GPL-3.0-or-later
// The index is a cache, so what matters is: it round-trips, it survives a restart, it misses when the
// folder changes, it never persists patient identifiers, and none of its failure modes may cost the
// viewer anything but a rescan. No DICOM files needed — SeriesInfo is plain data.
#include "clivus/index_store.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <cstdlib>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

clivus::SeriesInfo makeSeries(const std::string& uid, int width, std::vector<std::string> files)
{
    clivus::SeriesInfo s;
    s.seriesUid = uid;
    s.studyUid = "1.2.3";
    s.modality = "CT";
    s.patientName = "Doe^Jane";   // must never reach the cache
    s.studyDescription = "spike";
    s.seriesDescription = "axial 1.0";
    s.width = width;
    s.height = width;
    s.files = std::move(files);
    return s;
}

// setenv is POSIX-only; MSVC wants _putenv_s. The test sets CLIVUS_CACHE_DIR to keep its cache out
// of the real one, so the portable shim belongs here rather than in a core header.
void setEnv(const char* name, const std::string& value)
{
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);   // overwrite so a repeated call takes effect
#endif
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "clivus_index_test";
    const fs::path dir = root / "study";
    const fs::path cache = root / "cache";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(dir, ec);
    fs::create_directories(cache, ec);

    // The cache directory is an application concern; core obeys this variable (the GUI sets it from
    // QStandardPaths::CacheLocation). Tests point it at a temp dir so they never touch the real one.
    setEnv("CLIVUS_CACHE_DIR", cache.string());

    // Two data files, so the fingerprint is not an empty folder.
    for (const char* name : {"a.dcm", "b.dcm"}) std::ofstream(dir / name) << "not really dicom";

    const std::vector<clivus::SeriesInfo> written = {
        makeSeries("1.2.3.1", 512, {(dir / "a.dcm").string(), (dir / "b.dcm").string()}),
        makeSeries("1.2.3.2", 64, {(dir / "b.dcm").string()}),
    };

    // --- cache lives outside the data folder, under a hashed name
    {
        const std::string path = clivus::IndexStore::cachePathFor(dir.string());
        expect(!path.empty(), "a cache path resolves");
        expect(!contains(path, dir.string().c_str()), "the cache filename does not spell out the source path");
        expect(path.rfind(cache.string(), 0) == 0, "the cache lives in the application cache directory");
    }

    {
        clivus::IndexStore store;
        expect(store.open(dir.string()), "open a fresh index");
        expect(store.available(), "a fresh index is usable");
        expect(store.load().empty(), "fresh index has no series");
        expect(store.save(written), "saving a fresh scan succeeds");
    }

    // --- the database must not contain the patient name at all
    {
        const std::string path = clivus::IndexStore::cachePathFor(dir.string());
        std::ifstream db(path, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(db)), std::istreambuf_iterator<char>());
        expect(!contents.empty(), "cache database exists on disk");
        expect(!contains(contents, "Doe^Jane"), "patient name is not persisted in the cache");
        expect(!contains(contents, "patient_name"), "the schema has no patient_name column");
    }

    // --- restart: everything the UI needs comes back, except the patient name
    {
        clivus::IndexStore store;
        expect(store.open(dir.string()), "reopen an existing index");
        const auto loaded = store.load();
        expect(loaded.size() == 2, "two series survive a restart");
        if (loaded.size() == 2) {
            expect(loaded[0].seriesUid == "1.2.3.1", "series order preserved");
            expect(loaded[0].files.size() == 2, "file list preserved");
            expect(loaded[0].files[0] == written[0].files[0], "file path preserved");
            expect(loaded[1].width == 64, "geometry preserved");
            expect(loaded[1].patientName.empty(), "patient name is not restored from the cache");
            expect(loaded[0].modality == "CT", "modality preserved (needed for filtering)");
            expect(loaded[0].studyDescription == "spike", "study description preserved (shown in the browser)");
        }
    }

    {   // a new file makes the folder changed -> cache miss, not stale data
        std::ofstream(dir / "c.dcm") << "another";
        clivus::IndexStore store;
        expect(store.open(dir.string()), "open after change");
        expect(store.load().empty(), "changed folder invalidates the cache");
    }

    {   // and saving again after the change makes it a hit
        clivus::IndexStore store;
        expect(store.open(dir.string()), "open before re-save");
        expect(store.save(written), "re-save after a change");
        clivus::IndexStore again;
        again.open(dir.string());
        expect(again.load().size() == 2, "rescan re-establishes the cache");
    }

    {   // A series row with no series_file rows: the LEFT JOIN hands it back with a NULL path, which is
        // how the app ends up holding a SeriesInfo with an empty file list. It must round-trip intact,
        // because every caller that then does files.front() has to guard for it.
        clivus::IndexStore store;
        expect(store.open(dir.string()), "open before writing a fileless series");
        expect(store.save({makeSeries("1.2.3.3", 32, {})}), "save a fileless series");
        clivus::IndexStore again;
        expect(again.open(dir.string()), "reopen after writing a fileless series");
        const auto loaded = again.load();
        expect(loaded.size() == 1, "fileless series row survives the round trip");
        expect(!loaded.empty() && loaded[0].seriesUid == "1.2.3.3", "fileless series keeps its uid");
        expect(!loaded.empty() && loaded[0].files.empty(), "fileless series loads with an empty file list");
    }

    // --- corrupt database: open recovers by rebuilding, and the cache is usable again
    {
        const std::string path = clivus::IndexStore::cachePathFor(dir.string());
        {
            std::ofstream garbage(path, std::ios::binary | std::ios::trunc);
            garbage << "this is not sqlite";
        }
        clivus::IndexStore store;
        expect(store.open(dir.string()), "a corrupt cache is recovered, not fatal");
        expect(store.available(), "the rebuilt cache is usable");
        expect(store.load().empty(), "a rebuilt cache is a miss (the folder gets rescanned)");
        expect(store.save(written), "the rebuilt cache accepts a save");
    }

    // --- an old schema version is dropped at open, so stale sensitive columns do not linger
    {
        const std::string path = clivus::IndexStore::cachePathFor(dir.string());
        clivus::IndexStore::clearCache(dir.string());
        // Hand-write a v3-style database the old build would have produced, patient_name included.
        {
            std::ofstream legacy(path, std::ios::binary | std::ios::trunc);
            legacy << "SQLite format 3";
        }
        clivus::IndexStore store;
        expect(store.open(dir.string()), "open over an unusable old file");
        expect(store.load().empty(), "an old/unreadable file is a miss");
    }

    // --- the legacy in-folder index is removed on open (it carried patient names)
    {
        const std::string legacyPath = clivus::IndexStore::legacyPathFor(dir.string());
        { std::ofstream(legacyPath) << "SQLite format 3\0patient_name"; }
        expect(fs::exists(legacyPath), "legacy index planted in the data folder");
        clivus::IndexStore store;
        expect(store.open(dir.string()), "open removes the legacy index");
        expect(!fs::exists(legacyPath), "legacy in-folder index is gone (it stored patient names)");
    }

    // --- unusable cache directory: the same promise on every platform. A path whose parent is a
    // regular FILE cannot be created, which is portable (chmod 0500 makes a directory unwritable on
    // POSIX but has no effect on Windows, so the earlier version of this check silently passed on
    // Windows by not testing anything).
    {
        const fs::path blocker = root / "blocked-cache";
        { std::ofstream file(blocker); file << "not a directory"; }
        setEnv("CLIVUS_CACHE_DIR", (blocker / "index").string());
        clivus::IndexStore store;
        const bool opened = store.open(dir.string());
        expect(!opened, "an unusable cache directory yields an unusable cache, not a crash");
        expect(!store.available(), "unusable cache reports unavailable (caller scans uncached)");
        expect(!store.save(written), "saving without a database fails cleanly");
        expect(store.load().empty(), "loading without a database returns a miss");
        setEnv("CLIVUS_CACHE_DIR", cache.string());
    }

    // --- clearing. The store is scoped and therefore closed first: POSIX unlinks an open file,
    // Windows refuses to, and the test must exercise the same sequence the app uses.
    {
        {
            clivus::IndexStore store;
            store.open(dir.string());
            store.save(written);
        }
        expect(clivus::IndexStore::clearCache(dir.string()), "clearCache reports success");
        expect(!fs::exists(clivus::IndexStore::cachePathFor(dir.string())),
               "clearCache removes the cache file");
        clivus::IndexStore after;
        expect(after.open(dir.string()), "the cache can be recreated after clearing");
        expect(after.load().empty(), "a cleared cache is a miss");
    }

    fs::remove_all(root, ec);
    if (failures == 0) std::printf("index_store: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
