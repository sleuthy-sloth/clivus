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
    const double last  = center - 0.5 + (width - 1.0) / 2.0;
    const double scale = 255.0 / (width - 1.0);

    // shortcut: per-pixel float math instead of a 4096-entry LUT; fine at 1k^2 on
    // drag, switch to a LUT if window/level dragging ever shows up in the frame budget.
    for (size_t i = 0; i < pixels.size(); ++i) {
        const double x = pixels[i];
        double y;
        if (x <= first)      y = 0.0;
        else if (x > last)   y = 255.0;
        else                 y = (x - first) * scale;
        const double shown = invert ? 255.0 - y : y;   // MONOCHROME1 after clamping
        out[i] = uint8_t(std::clamp(shown, 0.0, 255.0) + 0.5);
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
