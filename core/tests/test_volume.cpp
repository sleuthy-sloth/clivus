// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry and stack ordering: testdata/volume holds 4 slices of 16x16 whose pixel value identifies the
// slice index, written in reverse z with InstanceNumber ascending in write order. So a loader that
// trusts InstanceNumber or filenames puts slice 3 where slice 0 belongs, and the value check fails.

#include "clivus/volume.h"
#include "clivus/series_index.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
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

    // Volume geometry fields from first slice
    assert(vol.hasGeometry == true);
    // The test volume is axial (row=(1,0,0), col=(0,1,0), normal=(0,0,1))
    assert(std::fabs(vol.orientRowX - 1.0) < 1e-6);
    assert(std::fabs(vol.orientRowY - 0.0) < 1e-6);
    assert(std::fabs(vol.orientRowZ - 0.0) < 1e-6);
    assert(std::fabs(vol.orientColX - 0.0) < 1e-6);
    assert(std::fabs(vol.orientColY - 1.0) < 1e-6);
    assert(std::fabs(vol.orientColZ - 0.0) < 1e-6);

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

    // Test orientationLabel function with known orientations
    // Axial: row=(1,0,0), col=(0,1,0) -> normal=(0,0,1)
    std::array<double, 6> iopAxial = {1, 0, 0, 0, 1, 0};
    assert(clivus::orientationLabel(iopAxial) == "Axial");
    
    // Sagittal: row=(0,1,0), col=(0,0,1) -> normal=(1,0,0)
    std::array<double, 6> iopSagittal = {0, 1, 0, 0, 0, 1};
    assert(clivus::orientationLabel(iopSagittal) == "Sagittal");
    
    // Coronal: row=(1,0,0), col=(0,0,1) -> normal=(0,1,0)
    std::array<double, 6> iopCoronal = {1, 0, 0, 0, 0, 1};
    assert(clivus::orientationLabel(iopCoronal) == "Coronal");
    
    // Oblique 45 degrees: row=(1,0,0), col=(0,0.707,0.707) -> normal=(0,-0.707,0.707) (not aligned with any axis)
    // |normal_z| = 0.707, |normal_y| = 0.707, neither > 0.707 threshold, so oblique
    std::array<double, 6> iopOblique = {1, 0, 0, 0, 0.70710678, 0.70710678};
    assert(clivus::orientationLabel(iopOblique) == "Oblique");
    
    // Invalid (zero) orientation
    std::array<double, 6> iopZero = {0, 0, 0, 0, 0, 0};
    assert(clivus::orientationLabel(iopZero) == "");

    std::printf("volume: all checks passed (4 slices, spacing %.1f/%.1f/%.1f mm, geometric order)\n",
                vol.spacingX, vol.spacingY, vol.spacingZ);
    return 0;
}
