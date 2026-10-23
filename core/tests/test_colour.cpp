// SPDX-License-Identifier: GPL-3.0-or-later
// Colour decode: the loader must hand back interleaved RGB whatever the file stores. The three
// objects written here hold the same red/green/blue wedge in three legal ways — RGB interleaved,
// RGB planar, and YBR_FULL interleaved — so the comparison between the decoded frames is the whole
// check for DCMTK's conversion. Every object is written by the test itself; nothing is vendored.
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

// Minimal valid secondary capture, three samples per pixel, uncompressed 8-bit. planar: 0 =
// interleaved (R,G,B per pixel), 1 = planar (all reds, then greens, then blues).
bool writeObject(const fs::path& path, const char* photometric, Uint16 planar,
                 const std::vector<Uint8>& pixels, const char* uidSuffix)
{
    const std::string sopInstanceUid = std::string("1.2.826.0.1.3680043.9.2.") + uidSuffix;
    DcmFileFormat ff;
    DcmDataset* ds = ff.getDataset();
    ds->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage);
    ds->putAndInsertString(DCM_SOPInstanceUID, sopInstanceUid.c_str());
    ds->putAndInsertString(DCM_StudyInstanceUID, "1.2.826.0.1.3680043.9.2");
    ds->putAndInsertString(DCM_SeriesInstanceUID, "1.2.826.0.1.3680043.9.2.7");
    ds->putAndInsertString(DCM_Modality, "OT");
    ds->putAndInsertString(DCM_PhotometricInterpretation, photometric);
    ds->putAndInsertUint16(DCM_SamplesPerPixel, 3);
    ds->putAndInsertUint16(DCM_PlanarConfiguration, planar);
    ds->putAndInsertUint16(DCM_Rows, 2);
    ds->putAndInsertUint16(DCM_Columns, 2);
    ds->putAndInsertUint16(DCM_BitsAllocated, 8);
    ds->putAndInsertUint16(DCM_BitsStored, 8);
    ds->putAndInsertUint16(DCM_HighBit, 7);
    ds->putAndInsertUint16(DCM_PixelRepresentation, 0);
    ds->putAndInsertUint8Array(DCM_PixelData, pixels.data(), Uint32(pixels.size()));
    DcmMetaInfo* meta = ff.getMetaInfo();
    meta->putAndInsertString(DCM_TransferSyntaxUID, UID_LittleEndianExplicitTransferSyntax);
    meta->putAndInsertString(DCM_MediaStorageSOPClassUID, UID_SecondaryCaptureImageStorage);
    meta->putAndInsertString(DCM_MediaStorageSOPInstanceUID, sopInstanceUid.c_str());
    return ff.saveFile(path.string().c_str(), EXS_LittleEndianExplicit).good();
}

}  // namespace

int main()
{
    using namespace clivus;

    const fs::path dir = fs::temp_directory_path() / "clivus_colour_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // 2x2: red, green, blue, then a mid-grey the two layouts must agree on. Four pixels, three
    // samples each.
    const Uint8 interleaved[12] = {
        255, 0, 0,   0, 255, 0,
        0, 0, 255,  128, 128, 128,
    };
    Uint8 planar[12];
    for (int i = 0; i < 4; ++i) {
        planar[i] = interleaved[i * 3];        // all reds first
        planar[4 + i] = interleaved[i * 3 + 1];
        planar[8 + i] = interleaved[i * 3 + 2];
    }
    // The same four pixels as YBR_FULL. DCMTK's converter (diybrpxt.h) treats Y as full range —
    // R = Y + 1.402(Cr-128) with no 16-235 expansion — so the object is written with the matching
    // forward transform (Y = 0.299R + 0.587G + 0.114B). Its lookup tables are integer-truncated,
    // which costs up to three counts on a 4:4:4 round trip, hence the slack below.
    auto clamp8 = [](double v) { return v < 0 ? 0 : v > 255 ? 255 : Uint8(v + 0.5); };
    Uint8 ybr[12];
    for (int i = 0; i < 4; ++i) {
        const double r = interleaved[i * 3], g = interleaved[i * 3 + 1], b = interleaved[i * 3 + 2];
        ybr[i * 3] = clamp8(0.299 * r + 0.587 * g + 0.114 * b);
        ybr[i * 3 + 1] = clamp8(128 - 0.168736 * r - 0.331264 * g + 0.5 * b);
        ybr[i * 3 + 2] = clamp8(128 + 0.5 * r - 0.418688 * g - 0.081312 * b);
    }

    const fs::path rgbPath = dir / "rgb.dcm";
    const fs::path planarPath = dir / "rgb_planar.dcm";
    const fs::path ybrPath = dir / "ybr_full.dcm";
    expect(writeObject(rgbPath, "RGB", 0, {interleaved, interleaved + 12}, "1"),
           "write the RGB interleaved object");
    expect(writeObject(planarPath, "RGB", 1, {planar, planar + 12}, "2"),
           "write the RGB planar object");
    expect(writeObject(ybrPath, "YBR_FULL", 0, {ybr, ybr + 12}, "3"),
           "write the YBR_FULL object");

    ImageFrame rgb, planarF, ybrF;
    expect(loadFrame(rgbPath.string(), rgb), "decode the RGB interleaved object");
    expect(loadFrame(planarPath.string(), planarF), "decode the RGB planar object");
    expect(loadFrame(ybrPath.string(), ybrF), "decode the YBR_FULL object");
    if (failures) {
        fs::remove_all(dir, ec);
        return 1;
    }

    expect(rgb.isColour() && planarF.isColour() && ybrF.isColour(),
           "all three colour objects decode as colour frames");
    expect(rgb.channels == 3 && rgb.width == 2 && rgb.height == 2 && rgb.valid(),
           "the RGB frame is 2x2 with three channels");
    expect(planarF.pixels == rgb.pixels,
           "PlanarConfiguration 1 decodes to the same interleaved RGB as 0");
    // YBR round trip through DCMTK's integer lookup tables: samples may land a few counts either
    // side of the exact value. Everything further would mean the conversion was skipped or shifted.
    int off = 0;
    for (size_t i = 0; i < rgb.pixels.size(); ++i) {
        const double d = ybrF.pixels[i] - rgb.pixels[i];
        if (d < -4.0 || d > 4.0) ++off;
    }
    expect(off == 0, "YBR_FULL decodes to the RGB of the same picture, within rounding");
    expect(ybrF.isColour(), "the YBR frame is colour");

    // A colour frame renders as itself: the window arguments are noise, the bytes are the samples.
    std::vector<uint8_t> shown(rgb.pixels.size());
    rgb.render(shown.data(), 0.0, 1.0);   // a window a greyscale frame would have crushed
    bool same = true;
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i] != uint8_t(rgb.pixels[i])) same = false;
    expect(same, "rendering a colour frame ignores the window and passes samples through");

    double c = 0, w = 0;
    rgb.fullWindow(c, w);
    expect(w >= 255.0, "fullWindow answers a display-range window for colour");

    fs::remove_all(dir, ec);
    if (failures == 0) std::printf("colour: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
