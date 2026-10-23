// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/roi.h"

#include <algorithm>
#include <cmath>

namespace clivus {
namespace {

// One implementation, two front doors: the mask arithmetic is what must be identical between the
// double and float callers (the float one is what the viewer uses).
template <typename Read>
RoiStats computeRoi(Read read, const int rows, const int cols, const RoiShape shape, int row0,
                    int col0, int row1, int col1, const double rowSpacingMm,
                    const double colSpacingMm)
{
    RoiStats stats;
    if (rows <= 0 || cols <= 0) return stats;

    // Dragging can go in any direction: normalise before using the bounds.
    if (row0 > row1) std::swap(row0, row1);
    if (col0 > col1) std::swap(col0, col1);
    row0 = std::max(0, std::min(row0, rows - 1));
    row1 = std::max(0, std::min(row1, rows - 1));
    col0 = std::max(0, std::min(col0, cols - 1));
    col1 = std::max(0, std::min(col1, cols - 1));

    const double centreRow = 0.5 * (row0 + row1);
    const double centreCol = 0.5 * (col0 + col1);
    const double radiusRow = 0.5 * (row1 - row0) + 0.5;
    const double radiusCol = 0.5 * (col1 - col0) + 0.5;

    double sum = 0.0;
    double sumSquares = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    int count = 0;

    for (int row = row0; row <= row1; ++row) {
        for (int col = col0; col <= col1; ++col) {
            if (shape == RoiShape::Ellipse) {
                const double dr = (row - centreRow) / radiusRow;
                const double dc = (col - centreCol) / radiusCol;
                if (dr * dr + dc * dc > 1.0) continue;   // outside the ellipse
            }
            const double value = static_cast<double>(
                read(static_cast<size_t>(row) * static_cast<size_t>(cols) + static_cast<size_t>(col)));
            if (count == 0) {
                minimum = value;
                maximum = value;
            } else {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
            sum += value;
            sumSquares += value * value;
            ++count;
        }
    }

    if (count == 0) return stats;
    stats.ok = true;
    stats.pixels = count;
    stats.mean = sum / count;
    const double variance = std::max(0.0, sumSquares / count - stats.mean * stats.mean);
    stats.stddev = std::sqrt(variance);
    stats.min = minimum;
    stats.max = maximum;
    if (rowSpacingMm > 0.0 && colSpacingMm > 0.0)
        stats.areaMm2 = count * rowSpacingMm * colSpacingMm;
    return stats;
}

}  // namespace

RoiStats roiStats(const std::vector<double>& pixels, const int rows, const int cols,
                  const RoiShape shape, int row0, int col0, int row1, int col1,
                  const double rowSpacingMm, const double colSpacingMm)
{
    if (rows <= 0 || cols <= 0) return {};
    if (static_cast<size_t>(rows) * static_cast<size_t>(cols) > pixels.size()) return {};
    return computeRoi([&pixels](size_t i) { return pixels[i]; }, rows, cols, shape, row0, col0, row1,
                      col1, rowSpacingMm, colSpacingMm);
}

RoiStats roiStats(const float* pixels, const int rows, const int cols, const RoiShape shape,
                  int row0, int col0, int row1, int col1, const double rowSpacingMm,
                  const double colSpacingMm)
{
    if (!pixels || rows <= 0 || cols <= 0) return {};
    return computeRoi([pixels](size_t i) { return pixels[i]; }, rows, cols, shape, row0, col0, row1,
                      col1, rowSpacingMm, colSpacingMm);
}

}  // namespace clivus
