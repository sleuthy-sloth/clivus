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
    // Patient ID is used to prove that two objects under one series UID belong to the same patient.
    // In memory only: the index cache deliberately does not persist it (see the cache policy note in
    // index_store.cpp), so a cached scan comes back with this empty.
    std::string patientId;
    std::string studyDescription;
    std::string seriesDescription;
    // True for the holding group of objects that carry no SeriesInstanceUID. They are kept and shown
    // rather than dropped, but never merged into a series that has one.
    bool quarantined = false;
    std::vector<std::string> files;   // sorted by InstanceNumber
    // Frames carried by each file, from NumberOfFrames; parallel to files, 1 when the tag is absent.
    // A series is browsed as one flat run of frames, so a 3-object series where one object holds 15
    // frames is 17 steps, not 3.
    std::vector<int> frameCounts;
    int width = 0;
    int height = 0;

    // Per-slice geometry (same length as files). These are header reads on the same pass as the scan,
    // no extra file opens.
    struct SliceGeometry {
        // ImagePositionPatient (origin in patient coordinates, mm)
        double originX = 0.0, originY = 0.0, originZ = 0.0;
        // ImageOrientationPatient row/column direction cosines (unit vectors in patient space)
        double orientRowX = 0.0, orientRowY = 0.0, orientRowZ = 0.0;
        double orientColX = 0.0, orientColY = 0.0, orientColZ = 0.0;
        // SliceLocation when present (mm)
        double sliceLocation = 0.0;
        // InstanceNumber
        long instanceNumber = 0;
        // True when IPP and IOP were both present and valid for this slice
        bool hasGeometry = false;
    };
    std::vector<SliceGeometry> sliceGeometry;

    // Total frames across every file. Falls back to the object count when frameCounts is empty, which
    // is what a SeriesInfo built by hand (or read from an index written before frames were tracked)
    // looks like.
    int totalFrames() const;

    // Maps a flat frame index onto the file that holds it and the frame within that file. False when
    // flat is out of range, so callers do not have to bounds-check first.
    bool locate(int flat, int& fileIndex, int& frameIndex) const;

    // Series-level geometry helpers
    // True when at least one slice has valid geometry (IOP + IPP)
    bool hasGeometry() const;
    // True when ALL slices have valid geometry (required for geometric ordering)
    bool hasFullGeometry() const;
    // Returns the slice normal (unit cross of orientRow x orientCol) from the first slice that has geometry
    // Returns {0,0,1} when no geometry is available
    std::array<double, 3> normal() const;
    // Returns the along-distance (dot(IPP, normal)) for each slice with geometry; empty when no geometry
    std::vector<double> alongDistances() const;
};

// Counters from one scan that are worth telling the user about. Counts only: no paths, no names, no
// identifiers, so they are safe to log or show in a status line.
struct ScanDiagnostics {
    size_t conflictSplits = 0;      // extra series created because one UID carried two identities
    size_t quarantinedObjects = 0;  // objects with no SeriesInstanceUID, kept outside the real series
    size_t skippedNonImage = 0;     // reports and structure sets skipped without a second full read
};

// Recursively scan dir for DICOM objects that carry pixel data, grouped by (StudyInstanceUID,
// SeriesInstanceUID) and split when one series UID is shown to carry two patient identities.
// Header-only: pixel data is not read during the scan.
// A scan that sees control->cancel returns an empty list, so check the flag before reporting "no
// images here" to anyone.
std::vector<SeriesInfo> scanDirectory(const std::string& dir, ScanControl* control = nullptr,
                                      ScanDiagnostics* diagnostics = nullptr);

// Convenience: cached scan (used by the UI). Reads the index file when fresh, otherwise scans.
std::vector<SeriesInfo> scanDirectoryCached(const std::string& dir, ScanControl* control);

// Decode one frame of one object. frameIndex is clamped into range, so a caller holding a stale index
// gets the last frame rather than an error; out.frameIndex/out.frameCount say what came back.
bool loadFrame(const std::string& file, ImageFrame& out, int frameIndex = 0);

// Orientation label from ImageOrientationPatient row/col cosines.
// Returns "Axial", "Sagittal", "Coronal", or "Oblique" based on which anatomical plane
// the slice normal is closest to. No R/L/A/P markers are added.
// Returns empty string when orientation cosines are not valid (all zeros or near-zero).
std::string orientationLabel(const std::array<double, 6>& iop);

}  // namespace clivus
