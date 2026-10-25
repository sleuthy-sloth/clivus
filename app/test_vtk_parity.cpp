// SPDX-License-Identifier: GPL-3.0-or-later
// Phase 2 cutover gate: the VTK display path must reproduce the legacy
// canvas's pixels. This runs offscreen — the window/level mapping is a CPU
// imaging filter (vtkImageMapToColors), not a render — so it runs in CI
// without a GL context. What it checks:
//   - greyscale: legacy ImageFrame::render() bytes vs the VTK pipeline
//     (bridge image + dicomLookupTable + vtkImageMapToColors), for a normal
//     frame and a MONOCHROME1 (inverted) frame;
//   - colour: the bridge's unsigned-char conversion vs legacy render() RGB
//     bytes, which must match exactly (same clamp-and-round formula).
// Tolerance is honest, not zero: the LUT has 4096 entries, so a scalar
// lands on a neighbouring entry and the two roundings can disagree by one
// grey level. The gate is: no pixel off by more than 2, and fewer than
// 0.1% off by more than 1. A systematic mapping error (wrong range,
// wrong sign, missing invert) fails this by hundreds of levels, not by one.
#include "clivus/image_frame.h"
#include "vtk_volume_bridge.h"

#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkLookupTable.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

clivus::ImageFrame makeGreyFrame(bool invert)
{
    clivus::ImageFrame frame;
    frame.width = 64;
    frame.height = 48;
    frame.channels = 1;
    frame.invert = invert;
    // A gradient that exercises the whole window: values from -1000 to 3000.
    frame.pixels.resize(size_t(64) * 48);
    for (size_t i = 0; i < frame.pixels.size(); ++i)
        frame.pixels[i] = -1000.0f + float(i) * (4000.0f / float(frame.pixels.size() - 1));
    frame.minValue = -1000.0f;
    frame.maxValue = 3000.0f;
    return frame;
}

void checkGrey(const clivus::ImageFrame& frame, double center, double width, const char* label)
{
    std::vector<uint8_t> legacy(frame.byteCount());
    frame.render(legacy.data(), center, width);

    vtkSmartPointer<vtkImageData> image = clivus::vtkImageDataFromFrame(frame);
    expect(image != nullptr, "bridge builds an image for the parity frame");
    if (image == nullptr) return;
    vtkSmartPointer<vtkImageMapToColors> mapper = vtkSmartPointer<vtkImageMapToColors>::New();
    mapper->SetInputData(image);
    mapper->SetLookupTable(clivus::dicomLookupTable(center, width, frame.invert));
    mapper->Update();
    vtkImageData* out = mapper->GetOutput();
    expect(out != nullptr, "VTK pipeline produces output");
    if (out == nullptr) return;
    const int components = out->GetNumberOfScalarComponents();
    expect(components >= 3, "VTK output carries RGB(A)");
    const auto* bytes = static_cast<const unsigned char*>(out->GetScalarPointer());
    expect(bytes != nullptr, "VTK output has scalar data");
    if (bytes == nullptr) return;

    int maxDiff = 0;
    size_t overOne = 0;
    for (size_t i = 0; i < legacy.size(); ++i) {
        const int diff = std::abs(int(bytes[i * size_t(components)]) - int(legacy[i]));
        if (diff > maxDiff) maxDiff = diff;
        if (diff > 1) ++overOne;
    }
    std::printf("parity %s: max diff %d, pixels off by >1: %zu/%zu\n", label, maxDiff, overOne,
                legacy.size());
    expect(maxDiff <= 2, "no pixel off by more than 2 grey levels");
    expect(overOne * 1000 < legacy.size(), "fewer than 0.1% of pixels off by more than 1");
}

}  // namespace

int main()
{
    // A narrow window (where VTK's own window/level filter would disagree
    // most) and a wide one, normal and inverted.
    checkGrey(makeGreyFrame(false), 40.0, 400.0, "grey narrow");
    checkGrey(makeGreyFrame(false), 1000.0, 4000.0, "grey wide");
    checkGrey(makeGreyFrame(true), 40.0, 400.0, "grey narrow inverted");
    checkGrey(makeGreyFrame(true), 1000.0, 4000.0, "grey wide inverted");

    {   // Colour: the bridge conversion must equal legacy render() exactly.
        clivus::ImageFrame frame;
        frame.width = 8;
        frame.height = 4;
        frame.channels = 3;
        frame.pixels.resize(size_t(8) * 4 * 3);
        for (size_t i = 0; i < frame.pixels.size(); ++i)
            frame.pixels[i] = float((i * 37) % 300) - 20.0f;   // spans the clamp range
        std::vector<uint8_t> legacy(frame.byteCount());
        frame.render(legacy.data(), 127.5, 256.0);
        vtkSmartPointer<vtkImageData> image = clivus::vtkImageDataFromFrame(frame);
        expect(image != nullptr, "bridge builds a colour image");
        if (image != nullptr) {
            const auto* bytes = static_cast<const unsigned char*>(image->GetScalarPointer());
            bool equal = bytes != nullptr;
            for (size_t i = 0; equal && i < legacy.size(); ++i) equal = bytes[i] == legacy[i];
            expect(equal, "colour bytes match legacy render() exactly");
        }
    }

    std::printf("vtk_parity: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
