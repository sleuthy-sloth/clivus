// SPDX-License-Identifier: GPL-3.0-or-later
// ROI statistics: rectangle and ellipse masks over known data, area only when spacing is known.
#include "clivus/roi.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

namespace {
bool near(double a, double b, double tolerance = 1e-9) { return std::fabs(a - b) <= tolerance; }
}  // namespace

int main()
{
    // A 4x4 ramp 0..15: known sum, known min/max, known count per mask.
    std::vector<double> ramp(16);
    for (int i = 0; i < 16; ++i) ramp[i] = i;

    // (a) rectangle covering the whole frame
    {
        const clivus::RoiStats stats =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Rectangle, 0, 0, 3, 3, 0.5, 0.5);
        CHECK(stats.ok);
        CHECK(stats.pixels == 16);
        CHECK(near(stats.mean, 7.5));
        CHECK(near(stats.min, 0.0));
        CHECK(near(stats.max, 15.0));
        // population SD of 0..15 = sqrt(21.25)
        CHECK(near(stats.stddev, std::sqrt(21.25)));
        CHECK(near(stats.areaMm2, 16 * 0.5 * 0.5));
    }

    // (b) a 2x2 rectangle in the corner: values 0,1,4,5 → mean 2.5, SD sqrt(4.25)
    {
        const clivus::RoiStats stats =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Rectangle, 0, 0, 1, 1, 1.0, 1.0);
        CHECK(stats.pixels == 4);
        CHECK(near(stats.mean, 2.5));
        CHECK(near(stats.stddev, std::sqrt(4.25)));
        CHECK(near(stats.areaMm2, 4.0));
    }

    // (c) reverse drag gives the same numbers (bounds are normalised, not trusted)
    {
        const clivus::RoiStats a =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Rectangle, 1, 1, 0, 0, 1.0, 1.0);
        const clivus::RoiStats b =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Rectangle, 0, 0, 1, 1, 1.0, 1.0);
        CHECK(a.pixels == b.pixels && near(a.mean, b.mean));
    }

    // (d) ellipse inside the same box covers fewer pixels than the rectangle and never zero
    {
        const clivus::RoiStats rect =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Rectangle, 0, 0, 3, 3, 1.0, 1.0);
        const clivus::RoiStats ellipse =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Ellipse, 0, 0, 3, 3, 1.0, 1.0);
        CHECK(ellipse.ok);
        CHECK(ellipse.pixels > 0);
        CHECK(ellipse.pixels < rect.pixels);   // corners excluded
        CHECK(ellipse.areaMm2 == ellipse.pixels * 1.0);
    }

    // (e) unknown spacing never invents an area
    {
        const clivus::RoiStats stats =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Rectangle, 0, 0, 3, 3, 0.0, 0.0);
        CHECK(stats.ok);
        CHECK(stats.areaMm2 == 0.0);
    }

    // (f) non-square pixels scale area correctly
    {
        const clivus::RoiStats stats =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Rectangle, 0, 0, 1, 1, 0.5, 0.75);
        CHECK(near(stats.areaMm2, 4 * 0.5 * 0.75));
    }

    // (g) a drag outside the frame is clamped, not read out of bounds
    {
        const clivus::RoiStats stats =
            clivus::roiStats(ramp, 4, 4, clivus::RoiShape::Rectangle, -5, -5, 99, 99, 1.0, 1.0);
        CHECK(stats.pixels == 16);
    }

    // (h) inconsistent arguments are refused rather than guessed
    {
        CHECK(!clivus::roiStats(ramp, 0, 4, clivus::RoiShape::Rectangle, 0, 0, 1, 1, 1, 1).ok);
        std::vector<double> tooSmall(4);
        CHECK(!clivus::roiStats(tooSmall, 4, 4, clivus::RoiShape::Rectangle, 0, 0, 1, 1, 1, 1).ok);
    }

    std::printf("roi: all checks passed\n");
    return 0;
}
