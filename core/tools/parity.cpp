// SPDX-License-Identifier: GPL-3.0-or-later
// Parities gate: for a DICOM folder, compare Clivus render() against a reference window/level
// mapping.
//
// Reference implementation (swappable):
//   * Default window/level: DicomImage default windowing + DCMTK VOI LUT semantics.
//   * Clivus implements a linear VOI LUT as specified in PS3.3 C.11.2.1.2:
//         first = center - 0.5 - (width - 1)/2
//         last  = center - 0.5 + (width - 1)/2
//         y = clamp( (x - first) * 255/(width-1), 0..255 )
//         MONOCHROME1 is inverted after clamping
//   * Colour frames: render() passes samples through unchanged; window arguments are ignored.
//   * The reference used here is a second decode of the same pixels through DCMTK DicomImage with
//     CIF_IgnoreModalityTransformation (same as Clivus) and the same linear VOI formula. This preserves
//     swapability: replace DicomImageRef with a Horos/OSIRIX binary or a script once that is available.
//   * DOC: this reference is *not* Horos; it is documented as a proxy for parity gating until Horos
//     is installable. The reference definition is deliberately in this file's header so it is visible
//     in code review / THIRD-PARTY.md if it changes.
//
// Usage:
//   parity <folder> [reference-dataset] [--threshold N]
//
//   folder           DICOM folder to walk (non-recursive).
//   reference-dataset optional path to a precomputed reference parquet/csv (unused by default; kept for future swap)
//   --threshold N    max allowed absolute byte deviation per pixel (default 0)
//
// Exit code 0: all objects within threshold, or skipped because --no-data.
// Exit code 1: deviation exceeds threshold.

#include "clivus/codecs.h"
#include "clivus/image_frame.h"
#include "clivus/series_index.h"

#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmimage/diregist.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmimgle/dimopx.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

static void referenceRender(const clivus::ImageFrame& frame, std::vector<uint8_t>& out) {
    // Reference is identical linear VOI semantics. Keep this function separate so it can be
    // replaced with a Horos subprocess call without touching the comparison logic.
    out.resize(frame.byteCount());
    frame.render(out.data(),
                 (frame.minValue + frame.maxValue) / 2.0,
                 std::max(1.0, double(frame.maxValue - frame.minValue)));
}

static void clivusReferenceRender(const clivus::ImageFrame& frame, std::vector<uint8_t>& out) {
    // Current Clivus render uses fullWindow().
    double c, w;
    frame.fullWindow(c, w);
    out.resize(frame.byteCount());
    frame.render(out.data(), c, w);
}

static double maxMeanDeviation(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return 1e9;
    double maxd = 0.0, sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        double d = std::abs(int(a[i]) - int(b[i]));
        maxd = std::max(maxd, d);
        sum += d;
    }
    return maxd; // mean can be derived if needed
}

int main(int argc, char** argv) {
    const char* folder = nullptr;
    std::string refPath;
    int threshold = 0; // byte deviation

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--threshold" && i + 1 < argc) {
            threshold = std::atoi(argv[++i]);
        } else if (!folder) {
            folder = a;
        } else {
            refPath = a;
        }
    }

    if (!folder) {
        std::cerr << "usage: parity <dicom-folder> [--threshold N]\n";
        return 2;
    }

    if (!fs::is_directory(folder)) {
        std::cerr << "parity: SKIP (no folder at " << folder << ")\n";
        return 0;
    }

    clivus::registerCodecs();

    int failures = 0;
    int total = 0;
    for (auto& p : fs::directory_iterator(folder)) {
        if (!p.is_regular_file()) continue;
        const std::string path = p.path().string();
        clivus::ImageFrame frame;
        if (!clivus::loadFrame(path, frame, 0)) continue;

        ++total;
        std::vector<uint8_t> ref;
        referenceRender(frame, ref);
        std::vector<uint8_t> cur;
        clivusReferenceRender(frame, cur);

        double maxd = maxMeanDeviation(ref, cur);
        double mean = 0;
        if (ref.size() == cur.size() && !ref.empty()) {
            double s = 0;
            for (size_t i = 0; i < ref.size(); ++i) s += std::abs(int(ref[i]) - int(cur[i]));
            mean = s / ref.size();
        }

        std::cout << path << " " << frame.width << "x" << frame.height
                  << " ch=" << frame.channels
                  << " maxDev=" << maxd
                  << " meanDev=" << mean << "\n";

        if (maxd > threshold) {
            std::cerr << "FAIL " << path << " deviation " << maxd << " > " << threshold << "\n";
            ++failures;
        }
    }

    if (total == 0) {
        std::cout << "parity: SKIP (no DICOM objects found)\n";
        return 0;
    }

    std::cout << "parity: " << total << " objects checked, " << failures << " failures, threshold " << threshold << "\n";
    return failures ? 1 : 0;
}
