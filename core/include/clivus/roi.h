// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

namespace clivus {

// Region-of-interest statistics over the stored (modality) pixel values.
//
// Why this lives in core: the arithmetic is the part that must be right, and it is testable without a
// window, a GL context or a file. The app supplies the pixel array it already has for display, plus
// the header's spacing; this returns the numbers a radiologist reads off the image.
//
// Units: values are whatever the pixel data means *after* the modality LUT the viewer applied to
// display it — HU for CT where a rescale exists, otherwise raw stored values. The caller decides and
// labels; this function never claims a unit.
//
// Standard deviation is the population form (divide by n), matching what most viewers report for an
// ROI. Area is the mask's pixel count times the in-plane pixel area, so an ellipse reports the area of
// the pixels it actually covered — not πab, which would overstate it at small radii.

enum class RoiShape { Rectangle, Ellipse };

struct RoiStats {
    bool ok = false;
    int pixels = 0;
    double mean = 0.0;
    double stddev = 0.0;
    double min = 0.0;
    double max = 0.0;
    double areaMm2 = 0.0;   // 0 when spacing is unknown: never invented
};

// pixels: row-major, rows*cols, already in display units.
// bounds: inclusive (row0,col0)-(row1,col1); the caller passes the drag rectangle in image pixels.
// spacing: millimetres per row/column; pass 0 when the header has none (areaMm2 stays 0).
// Returns ok=false when the mask covers no pixels or the arguments do not describe the buffer.
RoiStats roiStats(const std::vector<double>& pixels, int rows, int cols, RoiShape shape,
                  int row0, int col0, int row1, int col1, double rowSpacingMm, double colSpacingMm);

// Same computation straight from the decoded frame's buffer (float, modality units) so the view can
// hand over ImageFrame::pixels without a conversion pass of its own.
RoiStats roiStats(const float* pixels, int rows, int cols, RoiShape shape,
                  int row0, int col0, int row1, int col1, double rowSpacingMm, double colSpacingMm);

}  // namespace clivus
