// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry and stack ordering: testdata/volume holds 4 slices of 16x16 whose pixel value identifies the
// slice index, written in reverse z with InstanceNumber ascending in write order. So a loader that
// trusts InstanceNumber or filenames puts slice 3 where slice 0 belongs, and the value check fails.

#include "clivus/volume.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "testdata/volume";

    clivus::SeriesInfo series;
    for (auto& s : clivus::scanDirectory(dir)) series = s;
    if (series.files.empty()) {
        std::printf("volume: no DICOM in %s — skipped\n", dir.c_str());
        return 0;
    }
    assert(series.files.size() == 4);

    clivus::Volume vol;
    const bool loaded = clivus::loadVolume(series, vol);
    assert(loaded);
    assert(vol.valid());

    // Dimensions and geometry from PixelSpacing (1\1), position step 2.5 mm.
    assert(vol.width == 16);
    assert(vol.height == 16);
    assert(vol.depth == 4);
    assert(std::fabs(vol.spacingX - 1.0) < 1e-9);
    assert(std::fabs(vol.spacingY - 1.0) < 1e-9);
    assert(std::fabs(vol.spacingZ - 2.5) < 1e-9);

    // The whole point: depth k must be the slice at z = k*2.5, whose fill value is k. The filenames and
    // InstanceNumbers run the other way, so this also proves positions beat both.
    for (int z = 0; z < vol.depth; ++z) {
        const float v = vol.at(0, 0, z);
        if (v != float(z)) {
            std::printf("volume: depth %d holds slice %g, expected %d\n", z, double(v), z);
            return 1;
        }
        assert(vol.at(vol.width - 1, vol.height - 1, z) == float(z));
    }
    assert(vol.minValue == 0.0f);
    assert(vol.maxValue == 3.0f);

    std::printf("volume: all checks passed (4 slices, spacing %.1f/%.1f/%.1f mm, geometric order)\n",
                vol.spacingX, vol.spacingY, vol.spacingZ);
    return 0;
}
