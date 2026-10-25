// SPDX-License-Identifier: GPL-3.0-or-later
#include "vtk_plane_widget.h"

#include "canvas_widget.h"   // OverlayWidget (the shared raster overlay)
#include "style.h"
#include "vtk_volume_bridge.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>

#include <vtkCamera.h>
#include <vtkCellPicker.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <cmath>

VtkPlaneWidget::VtkPlaneWidget(Orientation orientation, QWidget* parent)
    : QVTKOpenGLNativeWidget(parent), m_orientation(orientation)
{
    setMinimumSize(320, 240);
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    setRenderWindow(m_renderWindow);
    m_viewer = vtkSmartPointer<vtkImageViewer2>::New();
    m_viewer->SetRenderWindow(m_renderWindow);
    applyOrientation();

    m_overlay = new OverlayWidget(this, [this](QPainter& p) { paintCrosshair(p); },
                                  [this] { return m_hasVolume && m_crosshairVisible; });
    m_overlay->setGeometry(rect());
}

void VtkPlaneWidget::applyOrientation()
{
    switch (m_orientation) {
    case Orientation::Axial:
        m_viewer->SetSliceOrientationToXY();
        m_sliceCount = m_dims[2];
        break;
    case Orientation::Coronal:
        m_viewer->SetSliceOrientationToXZ();
        m_sliceCount = m_dims[1];
        break;
    case Orientation::Sagittal:
        m_viewer->SetSliceOrientationToYZ();
        m_sliceCount = m_dims[0];
        break;
    }
}

void VtkPlaneWidget::setVolume(const clivus::Volume& volume)
{
    vtkSmartPointer<vtkImageData> image = clivus::vtkImageDataFromVolume(volume);
    if (image == nullptr) return;
    m_dims[0] = volume.width;
    m_dims[1] = volume.height;
    m_dims[2] = volume.depth;
    m_spacing[0] = volume.spacingX;
    m_spacing[1] = volume.spacingY;
    m_spacing[2] = volume.spacingZ;
    m_origin[0] = volume.originX;
    m_origin[1] = volume.originY;
    m_origin[2] = volume.originZ;
    m_direction[0][0] = volume.orientRowX;
    m_direction[0][1] = volume.orientColX;
    m_direction[0][2] = volume.normalX;
    m_direction[1][0] = volume.orientRowY;
    m_direction[1][1] = volume.orientColY;
    m_direction[1][2] = volume.normalY;
    m_direction[2][0] = volume.orientRowZ;
    m_direction[2][1] = volume.orientColZ;
    m_direction[2][2] = volume.normalZ;

    m_viewer->SetInputData(image);
    m_volumeImage = image;
    m_slabMode = false;
    m_hasVolume = true;
    applyOrientation();
    m_slice = std::clamp(m_sliceCount / 2, 0, std::max(0, m_sliceCount - 1));
    m_viewer->SetSlice(m_slice);
    m_viewer->SetColorWindow(m_width);
    m_viewer->SetColorLevel(m_center);
    m_viewer->GetRenderer()->ResetCamera();
    m_viewer->Render();
    update();
}

void VtkPlaneWidget::setSlabFrame(const clivus::ImageFrame& frame)
{
    vtkSmartPointer<vtkImageData> image = clivus::vtkImageDataFromFrame(frame);
    if (image == nullptr || !m_hasVolume) return;
    // A slab is a derived 2D view: it is always shown face-on, and the
    // slice control becomes the slab centre (the dialog rebuilds the slab
    // when the slider moves).
    m_viewer->SetInputData(image);
    m_viewer->SetSliceOrientationToXY();
    m_viewer->SetSlice(0);
    m_slabMode = true;
    m_sliceCount = 1;
    m_viewer->SetColorWindow(m_width);
    m_viewer->SetColorLevel(m_center);
    m_viewer->GetRenderer()->ResetCamera();
    m_viewer->Render();
    update();
}

void VtkPlaneWidget::restoreVolume()
{
    if (!m_hasVolume || m_volumeImage == nullptr) return;
    m_viewer->SetInputData(m_volumeImage);
    applyOrientation();
    m_slabMode = false;
    m_viewer->SetSlice(m_slice);
    m_viewer->SetColorWindow(m_width);
    m_viewer->SetColorLevel(m_center);
    m_viewer->Render();
}

void VtkPlaneWidget::setSlice(int slice)
{
    if (m_sliceCount <= 0) return;
    m_slice = std::clamp(slice, 0, m_sliceCount - 1);
    if (!m_slabMode) m_viewer->SetSlice(m_slice);
    m_viewer->Render();
}

void VtkPlaneWidget::setWindowLevel(double center, double width)
{
    m_center = center;
    m_width = std::max(1.0, width);
    m_viewer->SetColorLevel(m_center);
    m_viewer->SetColorWindow(m_width);
    m_viewer->Render();
}

void VtkPlaneWidget::resetCamera()
{
    m_viewer->GetRenderer()->ResetCamera();
    m_viewer->Render();
}

void VtkPlaneWidget::setCrosshair(const double world[3], bool visible)
{
    m_crosshair[0] = world[0];
    m_crosshair[1] = world[1];
    m_crosshair[2] = world[2];
    m_crosshairVisible = visible;
    if (m_overlay) m_overlay->update();
}

bool VtkPlaneWidget::pickWorld(QPoint widgetPoint, double world[3]) const
{
    vtkSmartPointer<vtkCellPicker> picker = vtkSmartPointer<vtkCellPicker>::New();
    // VTK display coordinates put the origin at the bottom-left, y up;
    // Qt puts it at the top-left, y down.
    if (!picker->Pick(double(widgetPoint.x()), double(height() - widgetPoint.y()), 0.0,
                      m_viewer->GetRenderer()))
        return false;
    double* pos = picker->GetPickPosition();
    world[0] = pos[0];
    world[1] = pos[1];
    world[2] = pos[2];
    return true;
}

void VtkPlaneWidget::paintCrosshair(QPainter& painter) const
{
    if (!m_crosshairVisible || !m_hasVolume) return;
    vtkRenderer* renderer = m_viewer->GetRenderer();
    renderer->SetWorldPoint(m_crosshair[0], m_crosshair[1], m_crosshair[2], 1.0);
    renderer->WorldToDisplay();
    double* display = renderer->GetDisplayPoint();
    const double qx = display[0];
    const double qy = double(height()) - display[1];   // VTK y-up -> Qt y-down
    painter.setPen(QPen(clivus::style::accent(), 1.0));
    painter.drawLine(QLineF(qx, 0, qx, height()));
    painter.drawLine(QLineF(0, qy, width(), qy));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(qx, qy), 4, 4);
}

void VtkPlaneWidget::mousePressEvent(QMouseEvent* e)
{
    double world[3];
    if (e->button() == Qt::LeftButton && pickWorld(e->pos(), world)) {
        m_drag = Drag::Crosshair;
        emit worldPointChanged(world[0], world[1], world[2]);
        return;
    }
    if (e->button() == Qt::RightButton && pickWorld(e->pos(), world)) {
        // Fixed anchor: every move picks the current point and shifts the
        // camera by (anchor - current), so the scene follows the cursor.
        m_drag = Drag::Pan;
        m_panAnchor[0] = world[0];
        m_panAnchor[1] = world[1];
        m_panAnchor[2] = world[2];
        setCursor(Qt::ClosedHandCursor);
        return;
    }
}

void VtkPlaneWidget::mouseMoveEvent(QMouseEvent* e)
{
    double world[3];
    if (m_drag == Drag::Crosshair && pickWorld(e->pos(), world)) {
        emit worldPointChanged(world[0], world[1], world[2]);
        return;
    }
    if (m_drag == Drag::Pan && pickWorld(e->pos(), world)) {
        vtkCamera* camera = m_viewer->GetRenderer()->GetActiveCamera();
        double pos[3], focal[3];
        camera->GetPosition(pos);
        camera->GetFocalPoint(focal);
        const double sx = m_panAnchor[0] - world[0];
        const double sy = m_panAnchor[1] - world[1];
        const double sz = m_panAnchor[2] - world[2];
        camera->SetPosition(pos[0] + sx, pos[1] + sy, pos[2] + sz);
        camera->SetFocalPoint(focal[0] + sx, focal[1] + sy, focal[2] + sz);
        m_viewer->Render();
        return;
    }
}

void VtkPlaneWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = Drag::None;
    unsetCursor();
}

void VtkPlaneWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    resetCamera();
}

void VtkPlaneWidget::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {   // ctrl+wheel zooms, like Horos
        vtkCamera* camera = m_viewer->GetRenderer()->GetActiveCamera();
        const double scale = camera->GetParallelScale();
        camera->SetParallelScale(std::clamp(scale * (e->angleDelta().y() > 0 ? 1.0 / 1.1 : 1.1),
                                            1.0, 100000.0));
        m_viewer->Render();
        return;
    }
    emit sliceStepRequested(e->angleDelta().y() > 0 ? -1 : 1);
}

void VtkPlaneWidget::resizeEvent(QResizeEvent* e)
{
    QVTKOpenGLNativeWidget::resizeEvent(e);
    if (m_overlay) m_overlay->setGeometry(rect());
}
