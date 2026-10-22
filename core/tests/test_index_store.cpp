// The index is a cache, so what matters is: it round-trips, it survives a restart, and it misses when
// the folder changes. No DICOM files needed — SeriesInfo is plain data.
#include "hw/index_store.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

hw::SeriesInfo makeSeries(const std::string& uid, int width, std::vector<std::string> files)
{
    hw::SeriesInfo s;
    s.seriesUid = uid;
    s.studyUid = "1.2.3";
    s.modality = "CT";
    s.patientName = "Doe^Jane";
    s.studyDescription = "spike";
    s.seriesDescription = "axial 1.0";
    s.width = width;
    s.height = width;
    s.files = std::move(files);
    return s;
}

}  // namespace

int main()
{
    const fs::path dir = fs::temp_directory_path() / "hw_index_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // Two data files, so the fingerprint is not an empty folder.
    for (const char* name : {"a.dcm", "b.dcm"}) {
        std::ofstream(dir / name) << "not really dicom";
    }

    const std::vector<hw::SeriesInfo> written = {
        makeSeries("1.2.3.1", 512, {(dir / "a.dcm").string(), (dir / "b.dcm").string()}),
        makeSeries("1.2.3.2", 64, {(dir / "b.dcm").string()}),
    };

    {
        hw::IndexStore store;
        expect(store.open(dir.string()), "open a fresh index");
        expect(store.load().empty(), "fresh index has no series");
        store.save(written);
    }

    {
        hw::IndexStore store;   // second instance: this is the restart case
        expect(store.open(dir.string()), "reopen an existing index");
        const auto loaded = store.load();
        expect(loaded.size() == 2, "two series survive a restart");
        if (loaded.size() == 2) {
            expect(loaded[0].seriesUid == "1.2.3.1", "series order preserved");
            expect(loaded[0].files.size() == 2, "file list preserved");
            expect(loaded[0].files[0] == written[0].files[0], "file path preserved");
            expect(loaded[1].width == 64, "geometry preserved");
            expect(loaded[1].patientName == "Doe^Jane", "strings preserved");
        }
    }

    {   // a new file makes the folder changed -> cache miss, not stale data
        std::ofstream(dir / "c.dcm") << "another";
        hw::IndexStore store;
        expect(store.open(dir.string()), "open after change");
        expect(store.load().empty(), "changed folder invalidates the cache");
    }

    {   // and saving again after the change makes it a hit
        hw::IndexStore store;
        store.open(dir.string());
        store.save(written);
        hw::IndexStore again;
        again.open(dir.string());
        expect(again.load().size() == 2, "rescan re-establishes the cache");
    }

    fs::remove_all(dir, ec);
    if (failures == 0) std::printf("index_store: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
