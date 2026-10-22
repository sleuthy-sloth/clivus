// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

#include "clivus/image_frame.h"

namespace clivus {

struct SeriesInfo {
    std::string seriesUid;
    std::string studyUid;
    std::string modality;
    std::string patientName;
    std::string studyDescription;
    std::string seriesDescription;
    std::vector<std::string> files;   // sorted by InstanceNumber
    int width = 0;
    int height = 0;
};

// Recursively scan dir for DICOM objects that carry pixel data, grouped by SeriesInstanceUID.
// Header-only: pixel data is not read during the scan.
std::vector<SeriesInfo> scanDirectory(const std::string& dir);

// Decode one object. Multi-frame objects yield frame 0 only.
bool loadFrame(const std::string& file, ImageFrame& out);

}  // namespace clivus
