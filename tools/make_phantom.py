#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a synthetic multi-slice CT phantom for MPR screenshots.

Why this exists: the committed corpus holds a single 128x128 CT object, so a coronal reformat of it is
one pixel tall — a black rectangle. Publishing that as "MPR" would misrepresent the feature, and
publishing a drawn picture would be worse. This writes a small, obviously synthetic volume (a body
cylinder with a dense spine and two low-density lungs) so the screenshot shows the application
reformatting a volume that actually has depth.

No patient data: every identifying field says SYNTHETIC. Values are Hounsfield-like (-1000 air,
40 soft tissue, 200 bone) so the viewer's default window and the ROI statistics behave as they would
on a real CT.

Usage: make_phantom.py <output-dir> [slices]
"""

import os
import sys

import numpy as np
from pydicom.dataset import Dataset, FileMetaDataset
from pydicom.uid import CTImageStorage, ExplicitVRLittleEndian, generate_uid

SLICES_DEFAULT = 64
ROWS = COLS = 128


def phantom_slice(z_norm: float) -> np.ndarray:
    """One axial slice: air, body ellipse, spine, two lungs. z_norm runs 0..1 through the stack."""
    image = np.full((ROWS, COLS), -1000, dtype=np.int16)
    yy, xx = np.mgrid[0:ROWS, 0:COLS]
    cy, cx = ROWS / 2.0, COLS / 2.0

    # Body: ellipse that narrows at the ends so the coronal/sagittal views have a recognisable shape.
    body_rx = 46.0 * (0.55 + 0.45 * np.sin(np.pi * min(max(z_norm, 0.0), 1.0)) ** 0.5)
    body_ry = 34.0
    body = ((xx - cx) / body_rx) ** 2 + ((yy - cy) / body_ry) ** 2 <= 1.0
    image[body] = 40

    # Two lungs: low density, positioned either side of the midline, fading out at the ends.
    lung_rx, lung_ry = 16.0, 22.0
    for sign in (-1.0, 1.0):
        lung = (((xx - (cx + sign * 20.0)) / lung_rx) ** 2 +
                ((yy - cy) / lung_ry) ** 2) <= 1.0
        image[lung & body] = -800

    # Spine: a dense column behind the lungs.
    spine = ((xx - cx) / 9.0) ** 2 + ((yy - (cy + body_ry * 0.55)) / 11.0) ** 2 <= 1.0
    image[spine & body] = 300

    # A soft-tissue lesion with known value, so an ROI over it reads the number it should.
    lesion = ((xx - (cx + 12.0)) / 6.0) ** 2 + ((yy - (cy - 10.0)) / 6.0) ** 2 <= 1.0
    image[lesion & body] = 120
    return image


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out_dir = sys.argv[1]
    slices = int(sys.argv[2]) if len(sys.argv) > 2 else SLICES_DEFAULT
    os.makedirs(out_dir, exist_ok=True)

    study_uid = generate_uid()
    series_uid = generate_uid()
    frame_uid = generate_uid()
    slice_spacing = 2.5

    for index in range(slices):
        z_norm = index / max(1, slices - 1)
        pixels = phantom_slice(z_norm)

        ds = Dataset()
        ds.file_meta = FileMetaDataset()
        ds.file_meta.MediaStorageSOPClassUID = CTImageStorage
        ds.file_meta.MediaStorageSOPInstanceUID = generate_uid()
        ds.file_meta.TransferSyntaxUID = ExplicitVRLittleEndian
        ds.file_meta.ImplementationClassUID = generate_uid()

        ds.SOPClassUID = CTImageStorage
        ds.SOPInstanceUID = ds.file_meta.MediaStorageSOPInstanceUID
        ds.StudyInstanceUID = study_uid
        ds.SeriesInstanceUID = series_uid
        ds.FrameOfReferenceUID = frame_uid
        ds.Modality = "CT"
        ds.PatientName = "SYNTHETIC^PHANTOM"
        ds.PatientID = "SYNTHETIC"
        ds.PatientBirthDate = ""
        ds.StudyDescription = "Synthetic MPR phantom"
        ds.SeriesDescription = "Synthetic MPR phantom"
        ds.StudyDate = "20240101"
        ds.StudyTime = "120000"
        ds.AccessionNumber = ""
        ds.InstanceNumber = index + 1
        ds.SeriesNumber = 1

        ds.Rows = ROWS
        ds.Columns = COLS
        ds.SamplesPerPixel = 1
        ds.PhotometricInterpretation = "MONOCHROME2"
        ds.BitsAllocated = 16
        ds.BitsStored = 16
        ds.HighBit = 15
        ds.PixelRepresentation = 1          # signed: Hounsfield values go below zero
        ds.RescaleSlope = 1
        ds.RescaleIntercept = 0             # values are already HU-like

        ds.PixelSpacing = [1.0, 1.0]
        ds.SliceThickness = slice_spacing
        ds.SpacingBetweenSlices = slice_spacing
        ds.ImageOrientationPatient = [1, 0, 0, 0, 1, 0]
        ds.ImagePositionPatient = [-COLS / 2.0, -ROWS / 2.0, index * slice_spacing]
        ds.SliceLocation = index * slice_spacing

        ds.PixelData = pixels.tobytes()
        ds.save_as(os.path.join(out_dir, f"slice_{index:03d}.dcm"), enforce_file_format=True)

    print(f"wrote {slices} synthetic slices ({ROWS}x{COLS}) to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
