// SPDX-License-Identifier: GPL-3.0-or-later
// Phase 3: one orthogonal plane of a volume on VTK, for the VTK MPR dialog.
// A vtkImageViewer2 per plane — the standard tool, which already knows how
// to orient a slice correctly (SetSliceOrientationToXY/XZ/YZ), so the
// dialog does not re-solve orientation. Two deliberate choices:
//   - Gestures are handled in Qt, not by the viewer's interactor (same rule
//     as the 2D canvas): left-drag moves the shared crosshair point, right
//     drag pans, wheel steps slices, ctrl+wheel zooms. The viewer's own
//     interactor style is never given events.
//   - The crosshair is NOT a VTK actor. It is drawn by the same raster
//     OverlayWidget the canvases use, from the shared world point
//     converted through the renderer's world-to-display transform — so it
//     can never z-fight with the slice, and its lines are never VTK text.
//   - Window/level here goes through the viewer's own mapping, which is
//     close to DICOM but not identical (its range is `width`, DICOM's is
//     `width - 1`; see the bridge header). For typical widths the
//     difference is under a grey level; the 2D canvas, which carries the
//     parity gate, uses the exact LUT instead. This is stated in the
//     dialog, not hidden.
#pragma once

#include <QVTKOpenGLNativeWidget.h>

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageViewer2.h>
#include <vtkSmartPointer.h>

#include "clivus/image_frame.h"
#include "clivus/volume.h"

class QMouseEvent;
class QPainter;
class QResizeEvent;
class QWheelEvent;
class OverlayWidget;

class VtkPlaneWidget : public QVTKOpenGLNativeWidget {
    Q_OBJECT
public:
    enum class Orientation { Axial, Coronal, Sagittal };

    explicit VtkPlaneWidget(Orientation orientation, QWidget* parent = nullptr);

    void setVolume(const clivus::Volume& volume);
    void setSlabFrame(const clivus::ImageFrame& frame);   // slab mode: one 2D frame
    void restoreVolume();   // back to the volume at the current slice, camera untouched
    bool slabMode() const { return m_slabMode; }
    void setSlice(int slice);
    int slice() const { return m_slice; }
    int sliceCount() const { return m_sliceCount; }
    void setWindowLevel(double center, double width);
    void resetCamera();

    // The shared crosshair point, in world (patient) coordinates. The
    // dialog owns the point; each plane draws where it projects.
    void setCrosshair(const double world[3], bool visible);
    bool hasVolume() const { return m_hasVolume; }

signals:
    void sliceStepRequested(int delta);
    // A click/drag on this plane picked a point on the slice.
    void worldPointChanged(double x, double y, double z);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void applyOrientation();
    bool pickWorld(QPoint widgetPoint, double world[3]) const;
    void paintCrosshair(QPainter& painter) const;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkImageViewer2> m_viewer;
    vtkSmartPointer<vtkImageData> m_volumeImage;   // kept so slab mode can return without reload
    OverlayWidget* m_overlay = nullptr;

    Orientation m_orientation;
    bool m_hasVolume = false;
    bool m_slabMode = false;
    int m_slice = 0;
    int m_sliceCount = 0;
    double m_center = 0.0, m_width = 1.0;

    // Volume geometry, for index/world conversion (direction orthonormal,
    // so the inverse is the transpose — no solver needed).
    int m_dims[3] = {0, 0, 0};
    double m_spacing[3] = {1.0, 1.0, 1.0};
    double m_origin[3] = {0.0, 0.0, 0.0};
    double m_direction[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    double m_crosshair[3] = {0.0, 0.0, 0.0};
    bool m_crosshairVisible = false;

    enum class Drag { None, Crosshair, Pan };
    Drag m_drag = Drag::None;
    double m_panAnchor[3] = {0.0, 0.0, 0.0};   // world point under the press, fixed
};
