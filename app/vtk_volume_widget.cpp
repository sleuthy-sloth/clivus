// SPDX-License-Identifier: GPL-3.0-or-later
#include "vtk_volume_widget.h"

#include "vtk_volume_bridge.h"

#include <algorithm>

VtkVolumeWidget::VtkVolumeWidget(QWidget* parent) : QVTKOpenGLNativeWidget(parent)
{
    setMinimumSize(320, 240);
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    setRenderWindow(m_renderWindow);
    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.0, 0.0, 0.0);
    m_renderWindow->AddRenderer(m_renderer);

    m_mapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
    m_color = vtkSmartPointer<vtkColorTransferFunction>::New();
    m_opacity = vtkSmartPointer<vtkPiecewiseFunction>::New();
    m_property = vtkSmartPointer<vtkVolumeProperty>::New();
    m_property->SetColor(m_color);
    m_property->SetScalarOpacity(m_opacity);
    m_property->ShadeOn();
    m_property->SetAmbient(0.2);
    m_property->SetDiffuse(0.9);
    m_property->SetInterpolationTypeToLinear();
    m_volumeActor = vtkSmartPointer<vtkVolume>::New();
    m_volumeActor->SetMapper(m_mapper);
    m_volumeActor->SetProperty(m_property);
    m_renderer->AddVolume(m_volumeActor);
}

void VtkVolumeWidget::setVolume(const clivus::Volume& volume)
{
    vtkSmartPointer<vtkImageData> image = clivus::vtkImageDataFromVolume(volume);
    if (image == nullptr) return;
    m_mapper->SetInputData(image);
    m_min = volume.minValue;
    m_max = volume.maxValue;
    m_center = (double(volume.minValue) + double(volume.maxValue)) / 2.0;
    m_width = std::max(1.0, double(volume.maxValue) - double(volume.minValue));
    m_unitDistance = (volume.spacingX + volume.spacingY + volume.spacingZ) / 3.0;
    if (m_unitDistance <= 0.0) m_unitDistance = 1.0;
    m_property->SetScalarOpacityUnitDistance(m_unitDistance);
    m_hasVolume = true;
    rebuildTransfer();
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VtkVolumeWidget::setWindowLevel(double center, double width)
{
    m_center = center;
    m_width = std::max(1.0, width);
    rebuildTransfer();
    m_renderWindow->Render();
}

void VtkVolumeWidget::rebuildTransfer()
{
    if (!m_hasVolume) return;
    // Grey ramp over the whole stored range, so the volume reads as the
    // anatomy it is rather than a colourised demo.
    m_color->RemoveAllPoints();
    m_color->AddRGBPoint(m_min, 0.0, 0.0, 0.0);
    m_color->AddRGBPoint(m_max, 1.0, 1.0, 1.0);
    // Opacity opens across the window: everything below the window is
    // transparent, everything above it is mostly opaque. Moving the window
    // is what sculpts the volume.
    const double lo = std::max(m_min, m_center - m_width / 2.0);
    const double hi = std::min(m_max, m_center + m_width / 2.0);
    m_opacity->RemoveAllPoints();
    m_opacity->AddPoint(m_min, 0.0);
    m_opacity->AddPoint(lo, 0.0);
    m_opacity->AddPoint(hi, 0.9);
    m_opacity->AddPoint(m_max, 0.9);
}

void VtkVolumeWidget::resetCamera()
{
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}
