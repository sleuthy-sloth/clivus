// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "clivus/series_index.h"

namespace clivus {

// A series decoded into one stack of equally sized slices, ordered geometrically along the slice
// normal. InstanceNumber is a display hint, not a position — real series arrive out of order, and MPR
// and volume rendering both produce wrong images if you trust the filenames.
struct Volume {
    int width = 0, height = 0, depth = 0;
    double spacingX = 1.0, spacingY = 1.0, spacingZ = 1.0;   // mm; X = column step, Y = row step
    std::vector<float> voxels;                               // slice-major: (z*height + y)*width + x
    float minValue = 0.0f, maxValue = 0.0f;

    // Slice origin (first slice IPP in patient coordinates, mm)
    double originX = 0.0, originY = 0.0, originZ = 0.0;
    // Slice orientation vectors (from first slice that has geometry)
    double orientRowX = 1.0, orientRowY = 0.0, orientRowZ = 0.0;  // row direction (patient space)
    double orientColX = 0.0, orientColY = 1.0, orientColZ = 0.0;  // column direction (patient space)
    // True when origin and orientation are populated from DICOM geometry
    bool hasGeometry = false;

    bool valid() const
    {
        return width > 0 && height > 0 && depth > 0
            && voxels.size() == size_t(width) * size_t(height) * size_t(depth);
    }
    float at(int x, int y, int z) const
    {
        return voxels[(size_t(z) * size_t(height) + size_t(y)) * size_t(width) + size_t(x)];
    }
};

// Decodes every slice of the series. Fails when the slices disagree on size or none decode.
bool loadVolume(const SeriesInfo& series, Volume& out);

}  // namespace clivus
