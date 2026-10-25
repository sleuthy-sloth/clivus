// SPDX-License-Identifier: GPL-3.0-or-later
#include "vtk_volume_bridge.h"

#include <vtkLookupTable.h>
#include <vtkMatrix3x3.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace clivus {

vtkSmartPointer<vtkImageData> vtkImageDataFromVolume(const Volume& volume)
{
    if (!volume.valid()) return nullptr;

    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(volume.width, volume.height, volume.depth);
    image->SetSpacing(volume.spacingX, volume.spacingY, volume.spacingZ);
    image->SetOrigin(volume.originX, volume.originY, volume.originZ);
    // Columns are the axes: I = row direction, J = column direction, K = normal.
    // Set precisely into a vtkMatrix3x3: vtkImageData has no double[3][3] overload
    // (9.3 offers vtkMatrix3x3*, or a flat 9-element array whose ordering is easy
    // to get backwards), and a transposed direction matrix mirrors the patient.
    vtkNew<vtkMatrix3x3> direction;
    direction->SetElement(0, 0, volume.orientRowX);
    direction->SetElement(1, 0, volume.orientRowY);
    direction->SetElement(2, 0, volume.orientRowZ);
    direction->SetElement(0, 1, volume.orientColX);
    direction->SetElement(1, 1, volume.orientColY);
    direction->SetElement(2, 1, volume.orientColZ);
    direction->SetElement(0, 2, volume.normalX);
    direction->SetElement(1, 2, volume.normalY);
    direction->SetElement(2, 2, volume.normalZ);
    image->SetDirectionMatrix(direction);
    image->AllocateScalars(VTK_FLOAT, 1);
    // Both layouts are x-fastest: VTK's (x + y*dimX + z*dimX*dimY) is the
    // volume's (z*height + y)*width + x, so this is a straight copy.
    std::memcpy(image->GetScalarPointer(), volume.voxels.data(),
                volume.voxels.size() * sizeof(float));
    return image;
}

vtkSmartPointer<vtkImageData> vtkImageDataFromFrame(const ImageFrame& frame)
{
    if (!frame.valid()) return nullptr;

    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(frame.width, frame.height, 1);
    image->SetSpacing(frame.pixelSpacingCol > 0.0 ? frame.pixelSpacingCol : 1.0,
                      frame.pixelSpacingRow > 0.0 ? frame.pixelSpacingRow : 1.0, 1.0);
    if (frame.isColour()) {
        image->AllocateScalars(VTK_UNSIGNED_CHAR, 3);
        auto* out = static_cast<unsigned char*>(image->GetScalarPointer());
        for (size_t i = 0; i < frame.pixels.size(); ++i)
            out[i] = static_cast<unsigned char>(std::clamp(frame.pixels[i], 0.0f, 255.0f) + 0.5f);
    } else {
        image->AllocateScalars(VTK_FLOAT, 1);
        std::memcpy(image->GetScalarPointer(), frame.pixels.data(),
                    frame.pixels.size() * sizeof(float));
    }
    return image;
}

vtkSmartPointer<vtkLookupTable> dicomLookupTable(double center, double width, bool invert)
{
    if (width < 1.0) width = 1.0;
    const double first = center - 0.5 - (width - 1.0) / 2.0;
    const double last = center - 0.5 + (width - 1.0) / 2.0;

    constexpr int kEntries = 4096;
    vtkSmartPointer<vtkLookupTable> table = vtkSmartPointer<vtkLookupTable>::New();
    table->SetNumberOfTableValues(kEntries);
    table->SetTableRange(first, last);
    for (int i = 0; i < kEntries; ++i) {
        // Explicit values, so Build() is deliberately NOT called: it would
        // regenerate the table from hue/saturation ranges and overwrite this.
        const double gray = double(i) / double(kEntries - 1);
        const double v = invert ? 1.0 - gray : gray;
        table->SetTableValue(i, v, v, v, 1.0);
    }
    return table;
}

}  // namespace clivus
