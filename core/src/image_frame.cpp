#include "hw/image_frame.h"

#include <algorithm>
#include <limits>

namespace hw {

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

void ImageFrame::render(uint8_t* out, double center, double width) const
{
    if (!out || !valid()) return;
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
        out[i] = uint8_t(std::clamp(y, 0.0, 255.0) + 0.5);
    }
}

void ImageFrame::fullWindow(double& center, double& width) const
{
    if (!valid()) { center = 0.0; width = 1.0; return; }
    center = (double(minValue) + double(maxValue)) / 2.0;
    width = std::max(1.0, double(maxValue) - double(minValue));
}

}  // namespace hw
