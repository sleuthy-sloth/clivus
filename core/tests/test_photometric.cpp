// SPDX-License-Identifier: GPL-3.0-or-later
// MONOCHROME1: the file says high stored values are displayed dark, so the grey ramp has to be
// flipped somewhere. The rule this test pins down: it is flipped at presentation (render()), not in
// the pixel values, so the stored values and the window/level numbers stay in modality units.
// Two objects are written with DCMTK — same pixels, differing PhotometricInterpretation — so the
// comparison is the whole check.
#include "clivus/image_frame.h"
#include "clivus/series_index.h"

#include <dcmtk/dcmdata/dctk.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

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

// Minimal but valid secondary capture: uncompressed 16-bit monochrome, no rescale, no padding.
bool writeObject(const fs::path& path, const char* photometric, const std::vector<Uint16>& pixels)
{
    const char* sopInstanceUid = "1.2.826.0.1.3680043.8.1055.9.1";
    DcmFileFormat ff;
    DcmDataset* ds = ff.getDataset();
    ds->putAndInsertString(DCM_SOPClassUID, UID_CTImageStorage);
    ds->putAndInsertString(DCM_SOPInstanceUID, sopInstanceUid);
    ds->putAndInsertString(DCM_StudyInstanceUID, "1.2.826.0.1.3680043.8.1055.9");
    ds->putAndInsertString(DCM_SeriesInstanceUID, "1.2.826.0.1.3680043.8.1055.9");
    ds->putAndInsertString(DCM_Modality, "CT");
    ds->putAndInsertString(DCM_PatientName, "Photometric^Test");
    ds->putAndInsertString(DCM_PhotometricInterpretation, photometric);
    ds->putAndInsertUint16(DCM_SamplesPerPixel, 1);
    ds->putAndInsertUint16(DCM_Rows, 1);
    ds->putAndInsertUint16(DCM_Columns, Uint16(pixels.size()));
    ds->putAndInsertUint16(DCM_BitsAllocated, 16);
    ds->putAndInsertUint16(DCM_BitsStored, 12);
    ds->putAndInsertUint16(DCM_HighBit, 11);
    ds->putAndInsertUint16(DCM_PixelRepresentation, 0);   // unsigned
    ds->putAndInsertUint16Array(DCM_PixelData, pixels.data(), pixels.size());

    DcmMetaInfo* meta = ff.getMetaInfo();
    meta->putAndInsertString(DCM_TransferSyntaxUID, UID_LittleEndianExplicitTransferSyntax);
    meta->putAndInsertString(DCM_MediaStorageSOPClassUID, UID_CTImageStorage);
    meta->putAndInsertString(DCM_MediaStorageSOPInstanceUID, sopInstanceUid);
    return ff.saveFile(path.string().c_str(), EXS_LittleEndianExplicit).good();
}

}  // namespace

int main()
{
    using namespace clivus;

    const fs::path dir = fs::temp_directory_path() / "clivus_photometric_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // 0 .. 1023 across one row: full dynamic window maps them 0, 85, 170, 255.
    const std::vector<Uint16> pixels = {0, 341, 682, 1023};
    const fs::path mono2Path = dir / "mono2.dcm";
    const fs::path mono1Path = dir / "mono1.dcm";
    expect(writeObject(mono2Path, "MONOCHROME2", pixels), "write the MONOCHROME2 object");
    expect(writeObject(mono1Path, "MONOCHROME1", pixels), "write the MONOCHROME1 object");

    ImageFrame mono2, mono1;
    expect(loadFrame(mono2Path.string(), mono2), "decode the MONOCHROME2 object");
    expect(loadFrame(mono1Path.string(), mono1), "decode the MONOCHROME1 object");
    if (!mono2.valid() || !mono1.valid()) {
        fs::remove_all(dir, ec);
        return 1;
    }

    expect(!mono2.invert, "MONOCHROME2 is not inverted");
    expect(mono1.invert, "MONOCHROME1 is marked for inversion");

    // The flip is presentation only: stored values, range and therefore the window are identical.
    expect(mono1.pixels == mono2.pixels, "MONOCHROME1 keeps the stored pixel values");
    expect(mono1.minValue == mono2.minValue && mono1.maxValue == mono2.maxValue,
           "MONOCHROME1 keeps the window range");
    double c1 = 0, w1 = 0, c2 = 0, w2 = 0;
    mono1.fullWindow(c1, w1);
    mono2.fullWindow(c2, w2);
    expect(c1 == c2 && w1 == w2, "MONOCHROME1 keeps the full-dynamic window");

    std::vector<uint8_t> shown2(pixels.size()), shown1(pixels.size());
    mono2.render(shown2.data(), c2, w2);
    mono1.render(shown1.data(), c1, w1);

    // The ramp runs the other way, sample for sample.
    expect(shown2[0] == 0 && shown2[3] == 255, "MONOCHROME2: low value is black, high is white");
    expect(shown1[0] == 255 && shown1[3] == 0, "MONOCHROME1: low value is white, high is black");
    bool complement = true;
    for (size_t i = 0; i < shown2.size(); ++i)
        if (int(shown1[i]) + int(shown2[i]) != 255) complement = false;
    expect(complement, "MONOCHROME1 output is the complement of MONOCHROME2");

    fs::remove_all(dir, ec);
    if (failures == 0) std::printf("photometric: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
