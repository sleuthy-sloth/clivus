// SPDX-License-Identifier: GPL-3.0-or-later
// Codec matrix: for every transfer syntax in testdata/corpus, is it decoded, and is the result the
// same pixels as the uncompressed original? Two jobs:
//   1. it says out loud which decoders this DCMTK build carries (the build decides, not DCMTK);
//   2. the lossless variants must reproduce MR_small.dcm exactly, so a DCMTK upgrade that silently
//      changes decompressed values fails the test instead of changing what a radiologist sees.
#include "clivus/codecs.h"
#include "clivus/series_index.h"

#include <dcmtk/dcmdata/dctk.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct Case {
    const char* file;
    const char* expect;   // codec name as it appears in codecSummary(), or "uncompressed"
    bool compareToMR;     // lossless variant of MR_small.dcm: must be pixel-identical
};

const Case kCases[] = {
    {"CT_small.dcm", "uncompressed", false},
    {"MR_small.dcm", "uncompressed", false},
    {"MR_small_jpeg_ls_lossless.dcm", "JPEG-LS", true},
    {"MR_small_RLE.dcm", "RLE", true},
    {"MR_small_jp2klossless.dcm", "JPEG 2000", true},
    {"JPEG2000.dcm", "JPEG 2000", false},
    // Lossy JPEG 2000 on a 12-bit object. The slice that used to sit here was a CQ500 study object,
    // whose terms do not allow redistributing it in a source repo; MR2_J2KI ships with pydicom.
    {"MR2_J2KI.dcm", "JPEG 2000", false},
    {"JPEGLSNearLossless_08.dcm", "JPEG-LS", false},
    {"JPEGLSNearLossless_16.dcm", "JPEG-LS", false},
    {"rtdose_rle.dcm", "RLE", false},
};

float maxAbsDiff(const clivus::ImageFrame& a, const clivus::ImageFrame& b)
{
    if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size())
        return 1e30f;
    float worst = 0.0f;
    for (size_t i = 0; i < a.pixels.size(); ++i)
        worst = std::max(worst, std::fabs(a.pixels[i] - b.pixels[i]));
    return worst;
}

int frameFailures = 0;

void expect(bool ok, const char* what)
{
    if (ok) return;
    std::printf("FAIL %s\n", what);
    ++frameFailures;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "testdata/corpus";
    if (!std::filesystem::is_directory(dir)) {
        std::printf("decode matrix: SKIPPED (no corpus at %s)\n", dir.c_str());
        return 0;   // generated/optional test data: absence is not a failure
    }

    const std::string codecs = clivus::codecSummary();
    std::printf("decoders in this build: %s\n\n", codecs.c_str());

    clivus::ImageFrame reference;
    const bool haveReference = clivus::loadFrame(dir + "/MR_small.dcm", reference);

    int ok = 0, unsupported = 0, failed = 0;
    for (const Case& c : kCases) {
        const std::string path = dir + "/" + c.file;
        const bool present = std::filesystem::exists(path);
        clivus::ImageFrame frame;
        const bool decoded = present && clivus::loadFrame(path, frame);

        // Is the codec this file needs even compiled in? Ask the file, not a table.
        std::string needed = "?";
        DcmFileFormat ff;
        if (present && ff.loadFile(path.c_str()).good() && ff.getDataset())
            needed = ff.getDataset()->getOriginalXfer() == EXS_LittleEndianExplicit ||
                             ff.getDataset()->getOriginalXfer() == EXS_LittleEndianImplicit ||
                             ff.getDataset()->getOriginalXfer() == EXS_BigEndianExplicit
                         ? "uncompressed"
                         : std::string(c.expect);
        const bool codecMissing = needed != "uncompressed" && codecs.find(needed) == std::string::npos;

        const char* status;
        std::string note;
        if (!present) {
            status = "MISSING";
            ++failed;
        } else if (decoded) {
            status = "OK";
            ++ok;
            if (c.compareToMR) {
                const float diff = haveReference ? maxAbsDiff(reference, frame) : 1e30f;
                note = diff == 0.0f ? "identical to MR_small.dcm"
                                    : "DIFFERS from MR_small.dcm by " + std::to_string(diff);
                if (diff != 0.0f) {
                    status = "FAIL";
                    --ok;
                    ++failed;
                }
            }
        } else if (codecMissing) {
            status = "UNSUPPORTED";
            ++unsupported;
            note = "no " + needed + " decoder in this build";
        } else {
            status = "FAIL";
            ++failed;
        }

        if (decoded)
            std::printf("%-30s %-12s %4dx%-4d range [%8.1f, %8.1f]  %s\n", c.file, status, frame.width,
                        frame.height, frame.minValue, frame.maxValue, note.c_str());
        else
            std::printf("%-30s %-12s %s\n", c.file, status, note.c_str());
    }

    // Multi-frame: frame 0 is not the whole object. The RT Dose carries 15 frames of 10x10 at 32 bits,
    // which is the case that decides both halves of this check — every frame must be reachable, and a
    // 32-bit object must still hand back one frame's worth of pixels rather than the whole stack.
    {
        const std::string path = dir + "/rtdose_rle.dcm";
        if (!std::filesystem::exists(path)) {
            std::printf("\nmulti-frame: SKIPPED (no rtdose_rle.dcm)\n");
        } else {
            clivus::ImageFrame first, mid, last, beyond;
            const bool ok0 = clivus::loadFrame(path, first, 0);
            const bool ok5 = clivus::loadFrame(path, mid, 5);
            const bool ok14 = clivus::loadFrame(path, last, 14);
            const bool okPast = clivus::loadFrame(path, beyond, 99);
            expect(ok0 && ok5 && ok14, "decode frames 0, 5 and 14 of the RT Dose");
            expect(first.frameCount == 15, "the decoded frame reports 15 frames in the object");
            expect(first.frameIndex == 0 && mid.frameIndex == 5 && last.frameIndex == 14,
                   "each decode reports which frame it is");
            expect(first.width == 10 && first.height == 10, "the frame is 10x10");
            expect(first.pixels.size() == 100,
                   "a 10x10 frame holds 100 pixels, not the whole 1500-value stack");
            expect(mid.pixels.size() == 100 && last.pixels.size() == 100,
                   "every frame holds one frame's worth of pixels");
            expect(first.pixels != mid.pixels, "frame 5 is not frame 0");
            expect(first.pixels != last.pixels && mid.pixels != last.pixels, "the frames differ from each other");
            expect(okPast && beyond.frameIndex == 14, "an out-of-range index clamps to the last frame");
            expect(beyond.pixels == last.pixels, "the clamped decode is the last frame's pixels");
            std::printf("\nmulti-frame: 15-frame 32-bit RT Dose | frame 0 range [%.0f, %.0f] | "
                        "frame 14 range [%.0f, %.0f]\n",
                        first.minValue, first.maxValue, last.minValue, last.maxValue);
        }
    }

    std::printf("\ndecode matrix: %d ok, %d unsupported by this build, %d failed\n", ok, unsupported, failed);
    if (unsupported)
        std::printf("note: codecs absent from this DCMTK build: %s missing\n",
                    codecs.find("JPEG 2000") == std::string::npos ? "JPEG 2000" : "(none)");
    return (failed == 0 && frameFailures == 0) ? 0 : 1;
}
