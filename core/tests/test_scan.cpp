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

    if (failures == 0) std::printf("scan: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
