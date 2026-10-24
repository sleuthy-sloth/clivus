// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace clivus {

// A single decoded image in modality units (rescale slope/intercept applied).
//
// Transforms, what is applied and what is not (PS3.3 C.11):
//
//   MODALITY LUT — applied in every shape it exists in: IDENTITY, linear (RescaleSlope /
//   RescaleIntercept), and non-linear (a Modality LUT Sequence, C.11.2.1.3 — the lookup runs in
//   loadFrame() before makeFrame, so pixels, min/max and window numbers are all in post-LUT
//   modality units). DCMTK could apply it, but it is deliberately disabled
//   (CIF_IgnoreModalityTransformation) so the loader stays in charge of the arithmetic and the
//   window numbers match what the values are. Dose objects carry no rescale tags; their
//   multiplier is DoseGridScaling, applied the same way. PALETTE COLOR (C.7.9) maps through the
//   file's own RGB LUTs at decode time, so palette frames arrive as display RGB.
//
//   VOI LUT — not read from the file. The default window is the full stored range ("full dynamic",
//   as Horos calls it) and the user moves it from there; a Window Center/Width in the file is not
//   honoured, nor is a non-linear VOI LUT Sequence. For MONOCHROME2 and MONOCHROME1 the window maps
//   linearly as in C.11.2.1.2; colour frames take no window at all.
struct ImageFrame {
    int width = 0;
    int height = 0;
    // 1 = grey, 3 = RGB. A colour frame holds R,G,B interleaved per pixel, already in display order
    // (DCMTK converts the file's YBR/planar variant before we see it). Colour is not windowed: DICOM
    // applies the VOI transform to greyscale only, so render() ignores the window for these.
    int channels = 1;
    std::vector<float> pixels;   // modality units, row-major, top-left origin
    float minValue = 0.0f;       // over non-padding pixels
    float maxValue = 0.0f;
    // Which frame of the source object this is, and how many that object carries. A single-frame
    // object is 0 of 1; a 15-frame RT Dose says 5 of 15. The scan reports the same count, so the
    // slice browser can step through frames without opening the object first.
    int frameIndex = 0;
    int frameCount = 1;
    // MONOCHROME1: the file says high stored values are displayed dark. The flip happens in
    // render(), so pixels, min/max and the window numbers stay in modality units.
    bool invert = false;
    // Physical pixel size from the object's Pixel Spacing, in mm. Row = vertical, column =
    // horizontal (DICOM orders Pixel Spacing as [row, column]). Zero means the header said
    // nothing — measurements must report 'spacing unknown' rather than inventing a value.
    double pixelSpacingRow = 0.0;
    double pixelSpacingCol = 0.0;

    // The modality transform that produced `pixels` from the stored values, so a readout can show both
    // numbers. rescaleKnown is false when a non-linear LUT supplied the values: then the stored number
    // cannot be recovered from the modality number, and a viewer that prints one anyway is inventing it.
    float rescaleSlope = 1.0f;
    float rescaleIntercept = 0.0f;
    bool rescaleKnown = true;
    // Calibrated unit for the modality value: "HU" for CT that carries a rescale pair, "Gy" for RT
    // Dose. Empty means "modality units, no calibrated unit claimed" — never a guess, because a
    // number with the wrong unit attached is worse than a number with none.
    std::string unit;

    bool isColour() const { return channels == 3; }

    // Bytes render() writes: one per pixel for grey, three for RGB.
    size_t byteCount() const { return size_t(width) * size_t(height) * size_t(channels); }

    bool valid() const
    {
        return width > 0 && height > 0 && channels > 0 && pixels.size() == byteCount();
    }

    // DICOM PS3.3 C.11.2.1.2 linear VOI LUT - the formula OsiriX/Horos uses.
    // out must hold byteCount() bytes.
    void render(uint8_t* out, double center, double width) const;

    // Window covering the whole stored range (Horos "full dynamic").
    void fullWindow(double& center, double& width) const;
};

// stored: raw values, row-major. slope/intercept are applied here; padding is excluded
// from min/max so the default window is not skewed by out-of-image values.
ImageFrame makeFrame(std::vector<float> stored, int width, int height,
                     float slope = 1.0f, float intercept = 0.0f,
                     bool hasPadding = false, float paddingValue = 0.0f);

// rgb: interleaved R,G,B, row-major, already in 0..255 display units. There is no modality or VOI
// transform to apply, so nothing is rescaled and nothing is windowed.
ImageFrame makeColourFrame(std::vector<float> rgb, int width, int height);

}  // namespace clivus
