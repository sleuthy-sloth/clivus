// SPDX-License-Identifier: GPL-3.0-or-later
// The Volume -> vtkImageData handover, checked without rendering: building
// the image needs no GL context, so this runs offscreen like the other
// core-adjacent tests. What it proves is the part Phase 2 parity depends
// on — dimensions, spacing, origin, orientation and values survive the
// handover exactly. It deliberately does NOT prove the rendered picture;
// that comparison (VTK canvas vs legacy canvas pixels) is Phase 2's gate.
#include "vtk_volume_bridge.h"

#include <vtkImageData.h>
#include <vtkMatrix3x3.h>
#include <vtkPointData.h>

#include <cstdio>

// vtkImageData has no GetDirectionMatrix(double[3][3]) overload: 9.3 returns a
// vtkMatrix3x3. Copy it out so the checks below read as the matrix they mean.
static void copyDirection(vtkImageData* image, double out[3][3])
{
    vtkMatrix3x3* m = image->GetDirectionMatrix();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) out[i][j] = m->GetElement(i, j);
}

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

clivus::Volume makeVolume()
{
    clivus::Volume v;
    v.width = 4;
    v.height = 3;
    v.depth = 2;
    v.spacingX = 0.5;
    v.spacingY = 0.25;
    v.spacingZ = 2.0;
    v.originX = 10.0;
    v.originY = 20.0;
    v.originZ = 30.0;
    v.voxels.resize(size_t(4) * 3 * 2);
    for (size_t i = 0; i < v.voxels.size(); ++i) v.voxels[i] = float(i) * 10.0f;
    v.minValue = 0.0f;
    v.maxValue = 230.0f;
    return v;
}

}  // namespace

int main()
{
    {   // Dimensions, spacing, origin and values round-trip exactly.
        const clivus::Volume v = makeVolume();
        vtkSmartPointer<vtkImageData> img = clivus::vtkImageDataFromVolume(v);
        expect(img != nullptr, "a valid volume produces an image");
        if (img == nullptr) {
            std::printf("vtk_handover: %d failure(s)\n", failures);
            return 1;
        }
        int dims[3] = {0, 0, 0};
        img->GetDimensions(dims);
        expect(dims[0] == 4 && dims[1] == 3 && dims[2] == 2, "dimensions match the volume");
        double spacing[3] = {0, 0, 0};
        img->GetSpacing(spacing);
        expect(spacing[0] == 0.5 && spacing[1] == 0.25 && spacing[2] == 2.0,
               "spacing matches the volume");
        double origin[3] = {0, 0, 0};
        img->GetOrigin(origin);
        expect(origin[0] == 10.0 && origin[1] == 20.0 && origin[2] == 30.0,
               "origin matches the volume");
        expect(img->GetScalarType() == VTK_FLOAT, "scalars stay float (modality values)");
        expect(img->GetNumberOfScalarComponents() == 1, "one component per voxel");
        // x-fastest on both sides: (x=3, y=2, z=1) is index (1*3 + 2)*4 + 3 = 23.
        const float* px = static_cast<const float*>(img->GetScalarPointer(3, 2, 1));
        expect(px != nullptr && *px == 230.0f, "voxel values land at the right indices");
    }
    {   // An unoriented volume maps through the identity direction matrix:
        // the volume's own defaults already form it.
        const clivus::Volume v = makeVolume();
        vtkSmartPointer<vtkImageData> img = clivus::vtkImageDataFromVolume(v);
        double dir[3][3] = {};
        copyDirection(img, dir);
        expect(dir[0][0] == 1.0 && dir[1][1] == 1.0 && dir[2][2] == 1.0 &&
                   dir[0][1] == 0.0 && dir[0][2] == 0.0 && dir[1][0] == 0.0 &&
                   dir[1][2] == 0.0 && dir[2][0] == 0.0 && dir[2][1] == 0.0,
               "default orientation is the identity matrix");
    }
    {   // A rotated volume carries its axes: row direction (0,1,0), column
        // direction (-1,0,0), normal (0,0,1). Columns are the axes, per the
        // convention in vtk_volume_bridge.h.
        clivus::Volume v = makeVolume();
        v.orientRowX = 0.0;
        v.orientRowY = 1.0;
        v.orientRowZ = 0.0;
        v.orientColX = -1.0;
        v.orientColY = 0.0;
        v.orientColZ = 0.0;
        v.normalX = 0.0;
        v.normalY = 0.0;
        v.normalZ = 1.0;
        vtkSmartPointer<vtkImageData> img = clivus::vtkImageDataFromVolume(v);
        double dir[3][3] = {};
        copyDirection(img, dir);
        expect(dir[0][0] == 0.0 && dir[1][0] == 1.0 && dir[2][0] == 0.0 &&
                   dir[0][1] == -1.0 && dir[1][1] == 0.0 && dir[2][1] == 0.0 &&
                   dir[0][2] == 0.0 && dir[1][2] == 0.0 && dir[2][2] == 1.0,
               "direction columns are [row, column, normal]");
    }
    {   // Invalid volumes produce nothing, never a half-built image.
        expect(clivus::vtkImageDataFromVolume(clivus::Volume{}) == nullptr,
               "an empty volume produces no image");
        clivus::Volume v = makeVolume();
        v.voxels.pop_back();
        expect(clivus::vtkImageDataFromVolume(v) == nullptr,
               "a voxel count that disagrees with the dimensions produces no image");
    }

    std::printf("vtk_handover: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
