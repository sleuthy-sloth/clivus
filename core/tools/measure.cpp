// SPDX-License-Identifier: GPL-3.0-or-later
// Headless smoke test + frame-cost measurement: scan a folder, decode, window, report timings.
// The GUI adds GL texture upload + draw on top of this; this isolates the CPU cost the
// canvas pays on every window/level change.
#include "clivus/codecs.h"
#include "clivus/index_store.h"
#include "clivus/series_index.h"
#include "clivus/volume.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: clivus_measure <dicom-folder> [iterations]\n");
        return 2;
    }
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 100;
    std::printf("decoders: %s\n", clivus::codecSummary().c_str());
    const auto t0 = std::chrono::steady_clock::now();
    const auto series = clivus::scanDirectoryCached(argv[1]);
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("scan: %zu series, %5.1f ms\n", series.size(),
                std::chrono::duration<double, std::milli>(t1 - t0).count());
    if (series.empty()) return 1;

    int failures = 0;
    for (const auto& s : series) {
        // A cached series row with no files (the index is a LEFT JOIN) has nothing to decode;
        // s.files.front() on it would be undefined behaviour.
        if (s.files.empty()) {
            std::printf("SKIP %s: series has no files in the index\n", s.seriesUid.c_str());
            continue;
        }
        clivus::ImageFrame frame;
        const auto d0 = std::chrono::steady_clock::now();
        if (!clivus::loadFrame(s.files.front(), frame)) {
            std::printf("FAIL decode %s (%s)\n", s.seriesUid.c_str(), s.files.front().c_str());
            ++failures;
            continue;
        }
        const auto d1 = std::chrono::steady_clock::now();

        double center = 0.0, width = 1.0;
        frame.fullWindow(center, width);
        std::vector<uint8_t> bytes(frame.pixels.size());
        const auto r0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) {
            center += (i % 2) * 0.0;                 // keep the window fixed: measure the render loop
            frame.render(bytes.data(), center, width);
        }
        const auto r1 = std::chrono::steady_clock::now();

        const double renderMs = std::chrono::duration<double, std::milli>(r1 - r0).count() / iterations;
        std::printf("%-4s %4dx%-4d %2zu obj  decode %6.1f ms  window+render %5.2f ms (%.0f fps cpu)  "
                    "range [%.0f, %.0f] WC %.0f WW %.0f  %s\n",
                    s.modality.c_str(), s.width, s.height, s.files.size(),
                    std::chrono::duration<double, std::milli>(d1 - d0).count(),
                    renderMs, renderMs > 0 ? 1000.0 / renderMs : 0.0,
                    double(frame.minValue), double(frame.maxValue), center, width,
                    s.seriesDescription.c_str());
    }
    // Stack cost: what MPR and volume rendering pay once, up front.
    for (const auto& s : series) {
        if (s.files.size() < 2) continue;
        clivus::Volume vol;
        const auto v0 = std::chrono::steady_clock::now();
        const bool ok = clivus::loadVolume(s, vol);
        const auto v1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(v1 - v0).count();
        if (!ok) {
            std::printf("stack %s: FAILED\n", s.seriesUid.c_str());
            ++failures;
            continue;
        }
        std::printf("stack %dx%dx%d  spacing %.2f/%.2f/%.2f mm  %zu slices in %6.1f ms (%.1f ms/slice)  "
                    "range [%.0f, %.0f]  %.0f MB\n",
                    vol.width, vol.height, vol.depth, vol.spacingX, vol.spacingY, vol.spacingZ,
                    size_t(vol.depth), ms, ms / vol.depth, double(vol.minValue), double(vol.maxValue),
                    double(vol.voxels.size() * sizeof(float)) / (1024.0 * 1024.0));
    }

    std::printf("%s\n", failures == 0 ? "all series decoded" : "DECODE FAILURES");
    return failures == 0 ? 0 : 1;
}
