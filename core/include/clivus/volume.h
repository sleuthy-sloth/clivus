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
    // Slice normal (cross of row/column cosines), patient space, from the first slice that has
    // geometry. Unit length when hasGeometry; (0,0,1) placeholder otherwise.
    double normalX = 0.0, normalY = 0.0, normalZ = 1.0;
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

// Whether the series carries usable per-slice geometry for MPR: every object reported
// ImagePositionPatient and ImageOrientationPatient. MPR entry points must disable themselves when
// this is false rather than render something that quietly lies about position.
bool hasUsableGeometry(const SeriesInfo& series);

// Which plane to reslice. AXIAL = the volume as scanned (rows/cols per slice), CORONAL =
// columns x depth, SAGITTAL = depth x rows. All through the series' own orientation vectors, so
// the three planes stay faithful to the geometry the scanner wrote.
enum class MprAxis { Axial, Coronal, Sagittal };

// Nearest-neighbour reslice of the volume at the given plane position (slice index for CORONAL and
// SAGITTAL, ignored for AXIAL where position is the whole stack). Returns a display-ready frame:
// width/height from the plane's extents, pixels in modality units, and the volume's min/max kept
// for windowing. Nearest-neighbour by choice for the first round: it invents nothing (a trilinear
// interpolated slice would blend tissue across slices), and the honest note in the overlay says
// what it is. Fails when the volume is invalid or the position is out of range.
bool resliceVolume(const Volume& volume, MprAxis axis, int position, ImageFrame& out);

}  // namespace clivus
