// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/image_frame.h"

#include <algorithm>
#include <limits>

namespace clivus {

ImageFrame makeFrame(std::vector<float> stored, int width, int height,
                     float slope, float intercept, bool hasPadding, float paddingValue)
{
    ImageFrame f;
    f.width = width;
    f.height = height;
    f.pixels = std::move(stored);
    if (!f.valid()) return f;

    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (size_t i = 0; i < f.pixels.size(); ++i) {
        const float raw = f.pixels[i];
        const float v = raw * slope + intercept;
        f.pixels[i] = v;
        if (hasPadding && raw == paddingValue) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (lo > hi) { lo = 0.0f; hi = 0.0f; }   // all-padding image
    f.minValue = lo;
    f.maxValue = hi;
    return f;
}

ImageFrame makeColourFrame(std::vector<float> rgb, int width, int height)
{
    ImageFrame f;
    f.width = width;
    f.height = height;
    f.channels = 3;
    f.pixels = std::move(rgb);
    // The display range of an 8-bit colour sample. Nothing windows a colour frame, so these are only
    // here to keep fullWindow() answering something truthful.
    f.minValue = 0.0f;
    f.maxValue = 255.0f;
    return f;
}

void ImageFrame::render(uint8_t* out, double center, double width) const
{
    if (!out || !valid()) return;

    if (channels == 3) {
        // No VOI transform applies to a colour image (PS3.3 C.11.2), so the window arguments are
        // ignored rather than silently shifting the colours.
        for (size_t i = 0; i < pixels.size(); ++i)
            out[i] = uint8_t(std::clamp(pixels[i], 0.0f, 255.0f) + 0.5f);
        return;
    }

    if (width < 1.0) width = 1.0;           // guard: a zero window would divide by zero

    const double first = center - 0.5 - (width - 1.0) / 2.0;

    if (width <= 1.0) {
        // Degenerate window: zero width, so the old three-way clamp is a step at first
        // (x <= first maps to 0, anything above to 255). Kept separate because the general
        // form would divide by (width - 1) == 0.
        for (size_t i = 0; i < pixels.size(); ++i) {
            double y = pixels[i] > first ? 255.0 : 0.0;
            if (invert) y = 255.0 - y;   // MONOCHROME1 after clamping
            out[i] = uint8_t(y + 0.5);
        }
        return;
    }
    const double scale = 255.0 / (width - 1.0);

    // Branchless form of the three-way clamp, and output-identical to it: (x - first) * scale is
    // <= 0 exactly when x <= first and >= 255 exactly when x >= last (scale > 0 here), so clamping
    // it reproduces both branches, and the middle expression is literally the same computation.
    // The old branched loop defeated vectorization; this one is what the scroll path runs once per
    // presented frame, so it is worth the shape. A LUT was considered and rejected: pixels are
    // floats (slope/intercept can leave fractional values), so an integer-indexed table would
    // quantize them and change output bytes the core tests pin.
    for (size_t i = 0; i < pixels.size(); ++i) {
        double y = std::clamp((double(pixels[i]) - first) * scale, 0.0, 255.0);
        if (invert) y = 255.0 - y;   // MONOCHROME1 after clamping
        out[i] = uint8_t(y + 0.5);
    }
}

void ImageFrame::fullWindow(double& center, double& width) const
{
    if (!valid()) { center = 0.0; width = 1.0; return; }
    if (channels == 3) { center = 127.5; width = 256.0; return; }   // ignored by render()
    center = (double(minValue) + double(maxValue)) / 2.0;
    width = std::max(1.0, double(maxValue) - double(minValue));
}

}  // namespace clivus
