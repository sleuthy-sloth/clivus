// VTK probe: our Volume -> vtkImageData -> GPU volume rendering, without Qt in the way.
// Proves three things on this platform: VTK links against the same DCMTK-decoded voxels, spacing and
// orientation survive the handover, and a 512^2 x N stack renders at interactive cost.
// Usage: vtk_probe <dicom-folder> [--shot out.png] [--size 900]

#include "hw/index_store.h"
#include "hw/volume.h"

#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPNGWriter.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>
#include <vtkWindowToImageFilter.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: vtk_probe <dicom-folder> [--shot out.png] [--size 900]\n");
        return 2;
    }
    std::string folder = argv[1], shot;
    int size = 900;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) size = std::atoi(argv[++i]);
    }

    const auto series = hw::scanDirectoryCached(folder);
    if (series.empty()) {
        std::printf("no series in %s\n", folder.c_str());
        return 1;
    }
    const hw::SeriesInfo& s = series.front();

    hw::Volume vol;
    const auto t0 = std::chrono::steady_clock::now();
    if (!hw::loadVolume(s, vol)) {
        std::printf("loadVolume failed for %s\n", s.seriesUid.c_str());
        return 1;
    }
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("volume %dx%dx%d spacing %.2f/%.2f/%.2f mm, %.0f MB, load %.0f ms\n",
                vol.width, vol.height, vol.depth, vol.spacingX, vol.spacingY, vol.spacingZ,
                double(vol.voxels.size() * sizeof(float)) / (1024.0 * 1024.0),
                std::chrono::duration<double, std::milli>(t1 - t0).count());

    vtkNew<vtkImageData> image;
    image->SetDimensions(vol.width, vol.height, vol.depth);
    image->SetSpacing(vol.spacingX, vol.spacingY, vol.spacingZ);
    image->AllocateScalars(VTK_FLOAT, 1);
    std::memcpy(image->GetScalarPointer(), vol.voxels.data(), vol.voxels.size() * sizeof(float));

    const double lo = vol.minValue;
    const double hi = vol.maxValue > lo ? vol.maxValue : lo + 1.0;
    const double span = hi - lo;

    vtkNew<vtkColorTransferFunction> color;
    color->AddRGBPoint(lo, 0.0, 0.0, 0.0);
    color->AddRGBPoint(lo + span * 0.25, 0.42, 0.18, 0.12);   // soft tissue
    color->AddRGBPoint(lo + span * 0.50, 0.78, 0.62, 0.48);
    color->AddRGBPoint(hi, 1.0, 1.0, 1.0);                    // bone

    vtkNew<vtkPiecewiseFunction> opacity;
    opacity->AddPoint(lo, 0.0);
    opacity->AddPoint(lo + span * 0.22, 0.0);
    opacity->AddPoint(lo + span * 0.40, 0.12);
    opacity->AddPoint(hi, 0.85);

    vtkNew<vtkVolumeProperty> property;
    property->SetColor(color);
    property->SetScalarOpacity(opacity);
    property->ShadeOff();
    property->SetInterpolationTypeToLinear();

    vtkNew<vtkSmartVolumeMapper> mapper;
    mapper->SetInputData(image);
    mapper->SetBlendModeToComposite();

    vtkNew<vtkVolume> volume;
    volume->SetMapper(mapper);
    volume->SetProperty(property);

    vtkNew<vtkRenderer> renderer;
    renderer->AddVolume(volume);
    renderer->SetBackground(0.08, 0.08, 0.10);

    vtkNew<vtkRenderWindow> window;
    window->SetSize(size, size);
    window->AddRenderer(renderer);
    if (!shot.empty()) window->SetOffScreenRendering(1);

    // Frame the volume from its bounding box so the probe is independent of origin conventions.
    const double* bounds = volume->GetBounds();
    renderer->GetActiveCamera()->SetPosition(bounds[1] + span, bounds[3] - span, bounds[5] + span);
    renderer->GetActiveCamera()->SetFocalPoint((bounds[0] + bounds[1]) / 2.0, (bounds[2] + bounds[3]) / 2.0,
                                               (bounds[4] + bounds[5]) / 2.0);
    renderer->GetActiveCamera()->SetViewUp(0, 0, 1);
    renderer->ResetCamera();
    renderer->ResetCameraClippingRange();

    const auto r0 = std::chrono::steady_clock::now();
    window->Render();
    const auto r1 = std::chrono::steady_clock::now();
    renderer->ResetCameraClippingRange();
    const auto r2 = std::chrono::steady_clock::now();
    window->Render();
    const auto r3 = std::chrono::steady_clock::now();
    std::printf("first frame %.0f ms (includes GPU upload), second %.0f ms renderer=%s\n",
                std::chrono::duration<double, std::milli>(r1 - r0).count(),
                std::chrono::duration<double, std::milli>(r3 - r2).count(),
                mapper->GetLastUsedRenderMode() == vtkSmartVolumeMapper::GPURenderMode ? "GPU" : "CPU");

    if (!shot.empty()) {
        vtkNew<vtkWindowToImageFilter> grab;
        grab->SetInput(window);
        grab->SetScale(1);
        grab->ReadFrontBufferOff();
        vtkNew<vtkPNGWriter> png;
        png->SetFileName(shot.c_str());
        png->SetInputConnection(grab->GetOutputPort());
        png->Write();
        std::printf("wrote %s\n", shot.c_str());
        return 0;
    }

    vtkNew<vtkRenderWindowInteractor> interactor;
    interactor->SetRenderWindow(window);
    interactor->Start();
    return 0;
}
