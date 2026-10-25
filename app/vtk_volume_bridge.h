// SPDX-License-Identifier: GPL-3.0-or-later
// Volume -> vtkImageData handover, shared by the VTK canvas widget and its
// test. Qt-free on purpose (core stays Qt-free; only app/ links VTK), and
// rendering-free: building the image needs no GL context, so the handover
// itself is testable offscreen. What this does NOT do is render anything —
// that is the widget's job, and it is where a GL context becomes necessary.
//
// Geometry convention, stated so Phase 2 parity tests can check it rather
// than guess: VTK's I axis is the fastest-varying index, i.e. the DICOM
// column index, so the direction matrix columns are [row direction,
// column direction, slice normal] — the DICOM row cosines, column cosines,
// and their cross product. The volume's own defaults already form the
// identity matrix, so an unoriented volume maps straight through.
#pragma once

#include "clivus/image_frame.h"
#include "clivus/volume.h"

#include <vtkImageData.h>
#include <vtkLookupTable.h>
#include <vtkSmartPointer.h>

namespace clivus {

// A vtkImageData copy of the volume's voxels: same dimensions, spacing,
// origin and orientation, float scalars copied (not referenced: the volume
// may move or die before the image does). Null when the volume is invalid
// (wrong size, or a voxel count that does not match its dimensions).
vtkSmartPointer<vtkImageData> vtkImageDataFromVolume(const Volume& volume);

// A vtkImageData copy of one frame, for the 2D canvas and for slab frames:
// greyscale as float modality values (w, h, 1), colour as interleaved
// unsigned-char RGB (legacy render()'s clamp-and-round, so the bytes match
// the legacy canvas exactly). Spacing carries the frame's pixel spacing so
// measurements in millimetres can be derived from it; the display itself
// works in pixel units, like the legacy canvas. Null on an invalid frame.
vtkSmartPointer<vtkImageData> vtkImageDataFromFrame(const ImageFrame& frame);

// The DICOM linear VOI LUT (PS3.3 C.11.2.1.2, the formula legacy
// ImageFrame::render() uses), as a VTK lookup table for
// vtkImageMapToColors: table range [first, last] with first =
// center - 0.5 - (width - 1)/2 and last = center - 0.5 + (width - 1)/2,
// 4096 ramp entries (reversed when the frame is MONOCHROME1). Using the
// table instead of vtkImageMapToWindowLevelColors is deliberate: VTK's
// window/level filter maps [level - window/2, level + window/2] with
// slope 255/window, which is close to DICOM but not identical (its range
// is `width`, DICOM's is `width - 1`), and it has no invert. The table
// reproduces legacy render() within the parity test's tolerance, and the
// parity test proves it rather than asserting it.
vtkSmartPointer<vtkLookupTable> dicomLookupTable(double center, double width, bool invert);

}  // namespace clivus
