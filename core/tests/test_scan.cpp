// SPDX-License-Identifier: GPL-3.0-or-later
// The scanner makes two promises to a UI that runs it on another thread: it says how far it has got,
// and a cancel stops it. Both are checked against the committed corpus, so the walk has real objects
// to parse rather than the empty folders the other tests use.
//
// The third check is the scan's second look at an object that appears to have no Pixel Data: a
// Pixel Data element with no value must still be turned away, and must not cost the scan the real
// image sitting next to it.
#include "clivus/series_index.h"

#include <dcmtk/dcmdata/dctk.h>

#include <cstdio>
#include <filesystem>
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

// A DICOM object with the attributes a viewer needs, and a Pixel Data element that is present but
// empty — the shape that sends the scan down its retry path.
bool writeEmptyPixelObject(const fs::path& file)
{
    DcmFileFormat ff;
    DcmDataset* ds = ff.getDataset();
    ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
    ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.9.1.3");
    ds->putAndInsertString(DCM_StudyInstanceUID, "1.2.826.0.1.3680043.9.1");
    ds->putAndInsertString(DCM_SeriesInstanceUID, "1.2.826.0.1.3680043.9.1.4");
    ds->putAndInsertString(DCM_Modality, "OT");
    ds->putAndInsertUint16(DCM_Rows, 2);
    ds->putAndInsertUint16(DCM_Columns, 2);
    ds->putAndInsertUint16(DCM_BitsAllocated, 8);
    ds->putAndInsertUint16(DCM_BitsStored, 8);
    ds->putAndInsertUint16(DCM_HighBit, 7);
    ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
    ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
    ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
    auto* empty = new DcmOtherByteOtherWord(DCM_PixelData);   // zero length, never given a value
    ds->insert(empty);
    return ff.saveFile(file.string().c_str(), EXS_LittleEndianExplicit).good();
}

// A complete object with configurable identity, so a folder can be built that contains exactly the
// ambiguity under test. Empty study/series/patient values are inserted as empty, and an empty
// SeriesInstanceUID is what the quarantine path is for.
bool writeObject(const fs::path& file, const char* study, const char* series, const char* patientId,
                 const char* patientName, int instance, bool omitSeries = false,
                 const char* charset = nullptr)
{
    DcmFileFormat ff;
    DcmDataset* ds = ff.getDataset();
    ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
    ds->putAndInsertString(DCM_SOPInstanceUID,
                           ("1.2.826.0.1.3680043.9.7." + std::to_string(instance)).c_str());
    // Declared before the text so a fixture can carry Latin-1 bytes the way an older export does.
    if (charset) ds->putAndInsertString(DCM_SpecificCharacterSet, charset);
    ds->putAndInsertString(DCM_StudyInstanceUID, study);
    if (!omitSeries) ds->putAndInsertString(DCM_SeriesInstanceUID, series);
    ds->putAndInsertString(DCM_PatientID, patientId);
    ds->putAndInsertString(DCM_PatientName, patientName);
    ds->putAndInsertString(DCM_InstanceNumber, std::to_string(instance).c_str());
    ds->putAndInsertString(DCM_Modality, "CT");
    ds->putAndInsertUint16(DCM_Rows, 2);
    ds->putAndInsertUint16(DCM_Columns, 2);
    ds->putAndInsertUint16(DCM_BitsAllocated, 16);
    ds->putAndInsertUint16(DCM_BitsStored, 12);
    ds->putAndInsertUint16(DCM_HighBit, 11);
    ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
    ds->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2");
    ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
    const Uint16 pixels[4] = {10, 20, 30, 40};
    ds->putAndInsertUint16Array(DCM_PixelData, pixels, 4);
    return ff.saveFile(file.string().c_str(), EXS_LittleEndianExplicit).good();
}

const clivus::SeriesInfo* findSeries(const std::vector<clivus::SeriesInfo>& all, const char* studyUid)
{
    for (const auto& s : all)
        if (s.studyUid == studyUid) return &s;
    return nullptr;
}

// A DICOM object of a class that cannot carry pixels (a structured report, a structure set). The scan
// must write it off without the unbounded second read it used to pay for every such object.
bool writeNonImageObject(const fs::path& file, const char* sopClass)
{
    DcmFileFormat ff;
    DcmDataset* ds = ff.getDataset();
    ds->putAndInsertString(DCM_SOPClassUID, sopClass);
    ds->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.9.8.1");
    ds->putAndInsertString(DCM_StudyInstanceUID, "1.2.826.0.1.3680043.9.8");
    ds->putAndInsertString(DCM_SeriesInstanceUID, "1.2.826.0.1.3680043.9.8.1");
    ds->putAndInsertString(DCM_Modality, "SR");
    // A value long enough to matter: the point of the case is that the scan never reads it in full.
    ds->putAndInsertString(DCM_SeriesDescription, "structured report");
    return ff.saveFile(file.string().c_str(), EXS_LittleEndianExplicit).good();
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: clivus_test_scan <corpus folder>\n");
        return 2;
    }
    const std::string corpus = argv[1];

    {
        clivus::ScanControl control;
        const auto series = clivus::scanDirectory(corpus, &control);
        expect(!series.empty(), "the corpus scans into at least one series");
        expect(control.examined.load() > 0, "the walk counted the entries it examined");
        expect(control.examined.load() >= series.size(), "every reported series came from an examined entry");
        expect(!control.cancel.load(), "a scan nobody cancelled leaves the flag down");
    }

    {   // Cancel already raised: the walk must not parse a single object.
        clivus::ScanControl control;
        control.cancel = true;
        const auto series = clivus::scanDirectory(corpus, &control);
        expect(series.empty(), "a scan that starts cancelled reports nothing");
        expect(control.examined.load() == 0, "a scan that starts cancelled examines nothing");
    }

    {   // One real object and one whose Pixel Data has no value, in a folder of their own.
        const fs::path dir = fs::temp_directory_path() / "hw_scan_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path good = fs::path(corpus) / "CT_small.dcm";
        fs::copy_file(good, dir / "good.dcm", fs::copy_options::overwrite_existing, ec);
        const bool wrote = writeEmptyPixelObject(dir / "empty.dcm");
        expect(wrote, "wrote the object with an empty Pixel Data element");

        const auto series = clivus::scanDirectory(dir.string());
        expect(series.size() == 1, "only the object with pixels is reported");
        if (series.size() == 1) {
            expect(series[0].files.size() == 1, "one object in the series");
            expect(series[0].files[0].find("good.dcm") != std::string::npos,
                   "the reported object is the one with pixel data");
        }
        fs::remove_all(dir, ec);
    }

    {   // A multi-frame object is one object and many slices. The count has to come from the header
        // alone — the browser decides how far it can scroll before it decodes anything.
        const auto series = clivus::scanDirectory(corpus);
        bool sawRtdose = false;
        bool sawSingle = false;
        for (const auto& s : series) {
            if (s.modality == "RTDOSE") {
                sawRtdose = true;
                expect(s.files.size() == 1, "the RT Dose series holds one object");
                expect(s.frameCounts.size() == s.files.size(),
                       "frame counts are parallel to the file list");
                expect(!s.frameCounts.empty() && s.frameCounts[0] == 15,
                       "the RT Dose object reports 15 frames from its header");
                expect(s.totalFrames() == 15, "totalFrames counts frames, not objects");
                int fileIndex = -1, frameIndex = -1;
                expect(s.locate(14, fileIndex, frameIndex) && fileIndex == 0 && frameIndex == 14,
                       "the last frame maps to frame 14 of the only object");
                expect(!s.locate(15, fileIndex, frameIndex), "there is no frame 15");
            }
            if (s.modality == "CT") {
                sawSingle = true;
                expect(s.frameCounts.size() == 1 && s.frameCounts[0] == 1,
                       "a single-frame object reports one frame, not zero");
                expect(s.totalFrames() == int(s.files.size()),
                       "a single-frame series steps once per object");
            }
        }
        expect(sawRtdose, "the corpus has an RT Dose object to check multi-frame scanning against");
        expect(sawSingle, "the corpus has a single-frame object to compare against");
    }

    {   // A bad subfolder (permission-denied) must not end the walk: files after it still index.
        // Recursion-critical for radiology folders that carry protected seriesdirectories next to
        // readable ones; the walk's old break-on-first-error dropped everything downstream.
        const fs::path dir = fs::temp_directory_path() / "hw_scan_denied";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        fs::copy_file(fs::path(corpus) / "CT_small.dcm", dir / "root.dcm",
                      fs::copy_options::overwrite_existing, ec);
        // Three denied subfolders so the walk meets one early in its order whatever the filesystem
        // sorts first.
        for (const fs::path denied : {dir / "01-denied", dir / "02-denied", dir / "03-denied"}) {
            fs::create_directories(denied, ec);
            fs::permissions(denied, fs::perms::none, ec);
        }
        const fs::path okDir = dir / "zz-ok";
        fs::create_directories(okDir, ec);
        fs::copy_file(fs::path(corpus) / "MR_small.dcm", okDir / "MR_small.dcm",
                      fs::copy_options::overwrite_existing, ec);

        const auto series = clivus::scanDirectory(dir.string());
        bool sawRoot = false, sawNested = false;
        for (const auto& s : series) {
            for (const std::string& f : s.files) {
                if (f.find("root.dcm") != std::string::npos) sawRoot = true;
                if (f.find("zz-ok") != std::string::npos) sawNested = true;
            }
        }
        expect(sawRoot, "the walk still indexes files at the top level");
        expect(sawNested, "files after a permission-denied subfolder are still indexed");
        for (const fs::path denied : {dir / "01-denied", dir / "02-denied", dir / "03-denied"}) {
            fs::permissions(denied, fs::perms::owner_all, ec);
        }
        fs::remove_all(dir, ec);
    }

    {   // One series UID under two study UIDs, two patients. The old grouping merged these into one
        // row and showed the first file's patient for all of them.
        const fs::path dir = fs::temp_directory_path() / "hw_scan_two_studies";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        expect(writeObject(dir / "a1.dcm", "1.2.826.0.1.3680043.9.A", "1.2.826.0.1.3680043.9.S",
                           "PID-A", "ALPHA^ONE", 1), "wrote the study A object");
        expect(writeObject(dir / "b1.dcm", "1.2.826.0.1.3680043.9.B", "1.2.826.0.1.3680043.9.S",
                           "PID-B", "BRAVO^TWO", 1), "wrote the study B object");

        clivus::ScanDiagnostics diag;
        const auto series = clivus::scanDirectory(dir.string(), nullptr, &diag);
        expect(series.size() == 2, "one series UID under two studies is two series, not one");
        expect(diag.conflictSplits == 1, "the shared series UID is reported as one conflict split");
        const auto* a = findSeries(series, "1.2.826.0.1.3680043.9.A");
        const auto* b = findSeries(series, "1.2.826.0.1.3680043.9.B");
        expect(a != nullptr && b != nullptr, "both studies are represented");
        if (a) {
            expect(a->patientId == "PID-A", "study A keeps its own patient");
            expect(a->files.size() == 1, "study A holds exactly its own object");
        }
        if (b) {
            expect(b->patientId == "PID-B", "study B keeps its own patient");
            expect(b->files.size() == 1, "study B holds exactly its own object");
        }
        if (a && b) expect(a->seriesUid != b->seriesUid, "the two groups do not share a series UID");
        fs::remove_all(dir, ec);
    }

    {   // One study, one series UID, two patient IDs. Patient identity is what splits it, and the
        // files of each patient stay with their own patient rather than being dealt out.
        const fs::path dir = fs::temp_directory_path() / "hw_scan_two_patients";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        expect(writeObject(dir / "p1.dcm", "1.2.826.0.1.3680043.9.C", "1.2.826.0.1.3680043.9.T",
                           "PID-A", "ALPHA^ONE", 1), "wrote patient A slice 1");
        expect(writeObject(dir / "p2.dcm", "1.2.826.0.1.3680043.9.C", "1.2.826.0.1.3680043.9.T",
                           "PID-B", "BRAVO^TWO", 2), "wrote patient B slice 1");
        expect(writeObject(dir / "p3.dcm", "1.2.826.0.1.3680043.9.C", "1.2.826.0.1.3680043.9.T",
                           "PID-A", "ALPHA^ONE", 3), "wrote patient A slice 2");

        clivus::ScanDiagnostics diag;
        const auto series = clivus::scanDirectory(dir.string(), nullptr, &diag);
        expect(series.size() == 2, "two patient IDs under one series UID is two series");
        expect(diag.conflictSplits == 1, "one split is reported, not one per file");
        int aFiles = 0, bFiles = 0;
        for (const auto& s : series) {
            if (s.patientId == "PID-A") aFiles += int(s.files.size());
            if (s.patientId == "PID-B") bFiles += int(s.files.size());
        }
        expect(aFiles == 2, "both of patient A's objects stay together");
        expect(bFiles == 1, "patient B's single object is its own group");
        fs::remove_all(dir, ec);
    }

    {   // An object with no SeriesInstanceUID used to be dropped on the floor. It must be kept, in a
        // group of its own, counted, and never folded into a series that has a UID.
        const fs::path dir = fs::temp_directory_path() / "hw_scan_no_series";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        expect(writeObject(dir / "named.dcm", "1.2.826.0.1.3680043.9.D", "1.2.826.0.1.3680043.9.U",
                           "PID-A", "ALPHA^ONE", 1), "wrote the object with a series UID");
        expect(writeObject(dir / "orphan1.dcm", "1.2.826.0.1.3680043.9.D", "", "PID-A", "ALPHA^ONE",
                           2, /*omitSeries=*/true), "wrote an object with no series UID");
        expect(writeObject(dir / "orphan2.dcm", "1.2.826.0.1.3680043.9.D", "", "PID-A", "ALPHA^ONE",
                           3, /*omitSeries=*/true), "wrote a second object with no series UID");

        clivus::ScanDiagnostics diag;
        const auto series = clivus::scanDirectory(dir.string(), nullptr, &diag);
        expect(diag.quarantinedObjects == 2, "both objects without a series UID are counted");
        expect(series.size() == 2, "the named series and the quarantine group");
        int quarantined = 0, named = 0;
        for (const auto& s : series) {
            if (s.quarantined) {
                quarantined += int(s.files.size());
                expect(s.files.size() == 2, "both orphans share the quarantine group");
            } else {
                named += int(s.files.size());
            }
        }
        expect(quarantined == 2, "the orphans are reachable, not dropped");
        expect(named == 1, "the named series holds only its own object");
        fs::remove_all(dir, ec);
    }

    {   // Determinism: grouping happens after the walk in a sorted order, so the same folder indexes
        // the same way twice. This is what makes the cache and the conflict counters trustworthy.
        const fs::path dir = fs::temp_directory_path() / "hw_scan_order";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        expect(writeObject(dir / "z.dcm", "1.2.826.0.1.3680043.9.E", "1.2.826.0.1.3680043.9.V",
                           "PID-A", "ALPHA^ONE", 3), "wrote slice 3");
        expect(writeObject(dir / "m.dcm", "1.2.826.0.1.3680043.9.E", "1.2.826.0.1.3680043.9.V",
                           "PID-A", "ALPHA^ONE", 1), "wrote slice 1");
        expect(writeObject(dir / "a.dcm", "1.2.826.0.1.3680043.9.E", "1.2.826.0.1.3680043.9.V",
                           "PID-A", "ALPHA^ONE", 2), "wrote slice 2");

        const auto first = clivus::scanDirectory(dir.string());
        const auto second = clivus::scanDirectory(dir.string());
        expect(first.size() == second.size(), "a rescan reports the same number of series");
        bool same = first.size() == second.size();
        for (size_t i = 0; same && i < first.size(); ++i) {
            same = first[i].seriesUid == second[i].seriesUid &&
                   first[i].files == second[i].files;
        }
        expect(same, "a rescan reports the same series in the same order with the same files");
        if (!first.empty()) {
            // Slices are ordered by InstanceNumber, not by the order the filesystem listed them.
            const auto& files = first[0].files;
            expect(files.size() == 3, "three slices in the series");
            if (files.size() == 3) {
                expect(files[0].find("m.dcm") != std::string::npos, "instance 1 comes first");
                expect(files[1].find("a.dcm") != std::string::npos, "instance 2 comes second");
                expect(files[2].find("z.dcm") != std::string::npos, "instance 3 comes third");
            }
        }
        fs::remove_all(dir, ec);
    }

    {   // Declared text encoding. A dataset that says ISO_IR 100 carries Latin-1 bytes, and everything
        // downstream decodes UTF-8, so the scan converts once where it reads. Without that, "Müller"
        // arrives in the UI as "M\uFFFDller" and reads as a broken file.
        const fs::path dir = fs::temp_directory_path() / "hw_scan_charset";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        expect(writeObject(dir / "latin1.dcm", "1.2.826.0.1.3680043.9.F", "1.2.826.0.1.3680043.9.W",
                           "PID-A", "M\xFCller", 1, /*omitSeries=*/false, "ISO_IR 100"),
               "wrote a Latin-1 object");
        expect(writeObject(dir / "utf8.dcm", "1.2.826.0.1.3680043.9.G", "1.2.826.0.1.3680043.9.X",
                           "PID-B", "M\xC3\xBCller", 1, /*omitSeries=*/false, "ISO_IR 192"),
               "wrote a UTF-8 object");

        const auto series = clivus::scanDirectory(dir.string());
        expect(series.size() == 2, "both objects scan");
        for (const auto& s : series) {
            if (s.studyUid == "1.2.826.0.1.3680043.9.F")
                expect(s.patientName == "M\xC3\xBCller",
                       "a Latin-1 patient name is converted to UTF-8, not left as replacement bytes");
            if (s.studyUid == "1.2.826.0.1.3680043.9.G")
                expect(s.patientName == "M\xC3\xBCller", "a UTF-8 patient name survives unchanged");
        }
        fs::remove_all(dir, ec);
    }

    {   // A structured report is not an image and never will be. The scan used to read the whole object
        // a second time to find that out; a known non-image storage class is now written off after the
        // bounded read, and the count says it happened.
        const fs::path dir = fs::temp_directory_path() / "hw_scan_non_image";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        expect(writeObject(dir / "image.dcm", "1.2.826.0.1.3680043.9.H", "1.2.826.0.1.3680043.9.Y",
                           "PID-A", "ALPHA^ONE", 1), "wrote an image object");
        expect(writeNonImageObject(dir / "report.dcm", "1.2.840.10008.5.1.4.1.1.88.33"),
               "wrote a comprehensive SR object");

        clivus::ScanDiagnostics diag;
        const auto series = clivus::scanDirectory(dir.string(), nullptr, &diag);
        expect(series.size() == 1, "the report does not become a series");
        expect(diag.skippedNonImage == 1, "the report is counted as skipped without a second read");

        // Control: an unknown class with no pixels still gets the full parse, and is not counted as a
        // known non-image — the deny-list must not grow into a filter that hides images it never saw.
        expect(writeNonImageObject(dir / "unknown.dcm", "1.2.826.0.1.3680043.9.99.1"),
               "wrote an object of an unrecognised storage class");
        clivus::ScanDiagnostics second;
        const auto afterUnknown = clivus::scanDirectory(dir.string(), nullptr, &second);
        expect(afterUnknown.size() == 1, "the unrecognised class is still not an image");
        expect(second.skippedNonImage == 1, "the unrecognised class is not counted as a known skip");
        fs::remove_all(dir, ec);
    }

    if (failures == 0) std::printf("scan: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
