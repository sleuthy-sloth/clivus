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
    // Frames carried by each file, from NumberOfFrames; parallel to files, 1 when the tag is absent.
    // A series is browsed as one flat run of frames, so a 3-object series where one object holds 15
    // frames is 17 steps, not 3.
    std::vector<int> frameCounts;
    int width = 0;
    int height = 0;

    // Total frames across every file. Falls back to the object count when frameCounts is empty, which
    // is what a SeriesInfo built by hand (or read from an index written before frames were tracked)
    // looks like.
    int totalFrames() const;

    // Maps a flat frame index onto the file that holds it and the frame within that file. False when
    // flat is out of range, so callers do not have to bounds-check first.
    bool locate(int flat, int& fileIndex, int& frameIndex) const;
};

// Recursively scan dir for DICOM objects that carry pixel data, grouped by SeriesInstanceUID.
// Header-only: pixel data is not read during the scan.
// A scan that sees control->cancel returns an empty list, so check the flag before reporting "no
// images here" to anyone.
std::vector<SeriesInfo> scanDirectory(const std::string& dir, ScanControl* control = nullptr);

// Decode one frame of one object. frameIndex is clamped into range, so a caller holding a stale index
// gets the last frame rather than an error; out.frameIndex/out.frameCount say what came back.
bool loadFrame(const std::string& file, ImageFrame& out, int frameIndex = 0);

}  // namespace clivus
