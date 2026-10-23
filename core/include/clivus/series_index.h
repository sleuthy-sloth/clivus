// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "clivus/image_frame.h"

namespace clivus {

// Knobs for a scan that runs somewhere other than the thread that started it: the worker bumps
// examined and reads cancel, the owner of the struct reads the one and writes the other, which is
// all the two need to agree on.
struct ScanControl {
    std::atomic<size_t> examined{0};   // directory entries looked at so far
    std::atomic<bool> cancel{false};
};

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
// A scan that sees control->cancel returns an empty list, so check the flag before reporting "no
// images here" to anyone.
std::vector<SeriesInfo> scanDirectory(const std::string& dir, ScanControl* control = nullptr);

// Decode one object. Multi-frame objects yield frame 0 only.
bool loadFrame(const std::string& file, ImageFrame& out);

}  // namespace clivus
