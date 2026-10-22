// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>

namespace clivus {

// A single decoded image in modality units (rescale slope/intercept applied).
struct ImageFrame {
    int width = 0;
    int height = 0;
    std::vector<float> pixels;   // modality units, row-major, top-left origin
    float minValue = 0.0f;       // over non-padding pixels
    float maxValue = 0.0f;

    bool valid() const { return width > 0 && height > 0 && pixels.size() == size_t(width) * size_t(height); }

    // DICOM PS3.3 C.11.2.1.2 linear VOI LUT - the formula OsiriX/Horos uses.
    // out must hold width*height bytes.
    void render(uint8_t* out, double center, double width) const;

    // Window covering the whole stored range (Horos "full dynamic").
    void fullWindow(double& center, double& width) const;
};

// stored: raw values, row-major. slope/intercept are applied here; padding is excluded
// from min/max so the default window is not skewed by out-of-image values.
ImageFrame makeFrame(std::vector<float> stored, int width, int height,
                     float slope = 1.0f, float intercept = 0.0f,
                     bool hasPadding = false, float paddingValue = 0.0f);

}  // namespace clivus
