#pragma once

#include <string>
#include <vector>

#include "hw/series_index.h"

namespace hw {

// A series decoded into one stack of equally sized slices, ordered geometrically along the slice
// normal. InstanceNumber is a display hint, not a position — real series arrive out of order, and MPR
// and volume rendering both produce wrong images if you trust the filenames.
struct Volume {
    int width = 0, height = 0, depth = 0;
    double spacingX = 1.0, spacingY = 1.0, spacingZ = 1.0;   // mm; X = column step, Y = row step
    std::vector<float> voxels;                               // slice-major: (z*height + y)*width + x
    float minValue = 0.0f, maxValue = 0.0f;

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

}  // namespace hw
