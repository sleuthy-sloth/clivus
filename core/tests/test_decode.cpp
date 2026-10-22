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
    {"693_J2KI.dcm", "JPEG 2000", false},
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

    std::printf("\ndecode matrix: %d ok, %d unsupported by this build, %d failed\n", ok, unsupported, failed);
    if (unsupported)
        std::printf("note: codecs absent from this DCMTK build: %s missing\n",
                    codecs.find("JPEG 2000") == std::string::npos ? "JPEG 2000" : "(none)");
    return failed == 0 ? 0 : 1;
}
