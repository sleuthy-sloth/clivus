// SPDX-License-Identifier: GPL-3.0-or-later
// Phase 4: GPU volume rendering on VTK, for the VTK MPR dialog. The
// pipeline is the one the headless vtk_probe already proved (volume data
// -> vtkSmartVolumeMapper, which picks the GPU ray-cast mapper when one is
// available), driven by the dialog's window/level: a grey colour ramp over
// the volume range and an opacity ramp that opens across the window, so
// moving the window is what sculpts the volume. Labelled experimental in
// the dialog; the probe's interactive-cost numbers are the CI gate before
// this is ever more than that.
#pragma once

#include <QVTKOpenGLNativeWidget.h>

#include <vtkColorTransferFunction.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkPiecewiseFunction.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>

#include "clivus/volume.h"

class QResizeEvent;

class VtkVolumeWidget : public QVTKOpenGLNativeWidget {
    Q_OBJECT
public:
    explicit VtkVolumeWidget(QWidget* parent = nullptr);

    void setVolume(const clivus::Volume& volume);
    void setWindowLevel(double center, double width);
    void resetCamera();
    bool hasVolume() const { return m_hasVolume; }

protected:
    void resizeEvent(QResizeEvent* e) override { QVTKOpenGLNativeWidget::resizeEvent(e); }

private:
    void rebuildTransfer();

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkSmartVolumeMapper> m_mapper;
    vtkSmartPointer<vtkVolume> m_volumeActor;
    vtkSmartPointer<vtkVolumeProperty> m_property;
    vtkSmartPointer<vtkColorTransferFunction> m_color;
    vtkSmartPointer<vtkPiecewiseFunction> m_opacity;

    bool m_hasVolume = false;
    double m_min = 0.0, m_max = 1.0;
    double m_center = 0.0, m_width = 1.0;
    double m_unitDistance = 1.0;
};
