// SPDX-License-Identifier: GPL-3.0-or-later
// Phase 2 demo: the VTK 2D canvas showing the middle slice of a real
// volume as a frame, outside the main window. Usage:
//   vtk_canvas_spike <dicom-folder>
// The format here is VTK's, not the app's: VTK 9 needs a core-profile 3.2+
// context, and the app's global 2.1 compatibility default (main.cpp) must
// not leak into this executable. The dual-format question for the real
// integration — legacy canvas on 2.1, VTK widget on core — is Phase 2's
// remaining integration work, and the migration doc says so.
#include "clivus/index_store.h"
#include "clivus/volume.h"
#include "vtk_canvas_widget.h"

#include <QApplication>
#include <QSurfaceFormat>

#include <algorithm>
#include <cstdio>

namespace {

// One slice of a volume as a frame, for the 2D canvas: pixels in modality
// units, the volume's min/max for the default window, spacing from the
// volume so the canvas's millimetre math works.
clivus::ImageFrame sliceFrame(const clivus::Volume& volume, int z)
{
    clivus::ImageFrame frame;
    frame.width = volume.width;
    frame.height = volume.height;
    frame.channels = 1;
    frame.frameIndex = z;
    frame.frameCount = volume.depth;
    frame.pixels.resize(size_t(volume.width) * size_t(volume.height));
    for (int y = 0; y < volume.height; ++y)
        for (int x = 0; x < volume.width; ++x)
            frame.pixels[size_t(y) * size_t(volume.width) + size_t(x)] = volume.at(x, y, z);
    frame.minValue = volume.minValue;
    frame.maxValue = volume.maxValue;
    frame.pixelSpacingRow = volume.spacingY;
    frame.pixelSpacingCol = volume.spacingX;
    return frame;
}

}  // namespace

int main(int argc, char** argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    if (argc < 2) {
        std::printf("usage: vtk_canvas_spike <dicom-folder>\n");
        return 2;
    }
    const auto series = clivus::scanDirectoryCached(argv[1]);
    if (series.empty()) {
        std::printf("no series in %s\n", argv[1]);
        return 1;
    }
    clivus::Volume vol;
    if (!clivus::loadVolume(series.front(), vol)) {
        std::printf("loadVolume failed for %s\n", series.front().seriesUid.c_str());
        return 1;
    }
    const int z = vol.depth / 2;
    std::printf("volume %dx%dx%d spacing %.2f/%.2f/%.2f mm, showing slice %d\n", vol.width,
                vol.height, vol.depth, vol.spacingX, vol.spacingY, vol.spacingZ, z);

    VtkCanvasWidget widget;
    int currentZ = z;
    widget.setFrame(sliceFrame(vol, currentZ));
    widget.setOverlay(QStringLiteral("VTK canvas spike — middle slice"),
                      QStringLiteral("drag: window/level | right-drag: pan | wheel: step slices"));
    // Wheel steps through the stack, like the main viewer: scrollRequested
    // carries the same sign convention (up = -1 = one slice back).
    QObject::connect(&widget, &VtkCanvasWidget::scrollRequested, &widget,
                     [&vol, &widget, &currentZ](int delta) {
                         const int next = std::clamp(currentZ + delta, 0, vol.depth - 1);
                         if (next == currentZ) return;
                         currentZ = next;
                         widget.setFrame(sliceFrame(vol, currentZ));
                     });
    widget.resize(900, 900);
    widget.show();
    return app.exec();
}
