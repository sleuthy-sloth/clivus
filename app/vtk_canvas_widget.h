// SPDX-License-Identifier: GPL-3.0-or-later
// Phase 2: the frame-based 2D canvas on VTK, API-compatible with the legacy
// CanvasWidget where it matters (frames, window/level, gestures, tools,
// measurements, ROIs, overlay text, signals) so the main window can one day
// swap canvases without relearning how to talk to one.
//
// What VTK does here, and what it does not:
//   - VTK displays the image (vtkImageSliceMapper + vtkImageSlice) and owns
//     the camera. Pan/zoom are the legacy canvas's own state (m_zoom, m_pan
//     in widget pixels, same fit formula), driven into the camera by
//     syncCamera() — so framing matches the legacy canvas exactly, and
//     mapToFrame/frameToWidget are the legacy formulas verbatim.
//   - Window/level is NOT vtkImageMapToWindowLevelColors. That filter maps
//     [level - window/2, level + window/2] with slope 255/window; DICOM's
//     linear VOI maps a range of `width - 1` and MONOCHROME1 inverts it.
//     The widget uses the shared dicomLookupTable() through
//     vtkImageMapToColors instead, and the parity test proves the bytes
//     match legacy render() within tolerance.
//   - Overlays (corner text, measurements, ROIs) are NOT VTK actors and
//     never vtkTextActor — after the 0.4.1 garbled-glyph report, text goes
//     through the same raster OverlayWidget the legacy canvas uses, fed by
//     this widget's own paintOverlays(). The drawing code is a port of the
//     legacy canvas's, kept in step with it; the two canvases share the
//     overlay widget class, not the drawing code.
//   - VTK's own interactor is silenced (vtkInteractorStyleUser, and the
//     Qt event handlers do not forward to it): every gesture is handled in
//     Qt, mirroring the legacy canvas exactly, so there is one interaction
//     path and no double-applied drag.
//   - VTK displays row 0 at the bottom (world +y is up); the legacy canvas
//     draws row 0 at the top. The display image's rows are reversed at
//     setFrame() so the picture matches — the stored frame is untouched,
//     and pixelInfoAt/measurement math still reads the original frame.
#pragma once

#include <QVTKOpenGLNativeWidget.h>

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageMapToColors.h>
#include <vtkImageSlice.h>
#include <vtkImageSliceMapper.h>
#include <vtkLookupTable.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

#include <QFont>
#include <QPainter>
#include <QPointF>
#include <QRect>
#include <QString>

#include <utility>
#include <vector>

#include "clivus/image_frame.h"

class QKeyEvent;
class QMouseEvent;
class QResizeEvent;
class QWheelEvent;
class OverlayWidget;

class VtkCanvasWidget : public QVTKOpenGLNativeWidget {
    Q_OBJECT
public:
    explicit VtkCanvasWidget(QWidget* parent = nullptr);

    enum class ToolHint { WindowLevel, Pan, Pointer, Measure, RoiRect, RoiEllipse };
    void setToolHint(ToolHint hint);
    ToolHint toolHint() const { return m_toolHint; }

    struct DistanceMeasurement {
        QPointF p1, p2;   // widget coordinates
        double distance_mm = 0.0;
        bool spacing_known = false;
    };
    struct AngleMeasurement {
        QPointF p1, p2, p3;   // widget coordinates, p2 is the vertex
        double angle_deg = 0.0;
    };
    struct RoiAnnotation {
        bool ellipse = false;
        int row0 = 0, col0 = 0, row1 = 0, col1 = 0;
        int pixels = 0;
        double mean = 0.0, stddev = 0.0, min = 0.0, max = 0.0;
        double areaMm2 = 0.0;
        bool areaKnown = false;
    };
    struct PixelInfo {
        bool valid = false;
        int row = 0, col = 0;   // DICOM counts pixels from 1
        double modality = 0.0;
        bool storedKnown = false;
        double stored = 0.0;
        QString unit;
    };

    void setFrame(clivus::ImageFrame frame);
    void setPixelSpacing(double rowSpacingMm, double colSpacingMm);
    void setWindowLevel(double center, double width);
    double windowCenter() const { return m_center; }
    double windowWidth() const { return m_width; }
    void setOverlay(const QString& topLeft, const QString& topRight);
    void resetView();   // full dynamic window + zoom/pan back to fit
    void fitView();     // zoom/pan back to fit, window untouched

    bool hasValidFrame() const { return m_frame.valid(); }
    const clivus::ImageFrame& frame() const { return m_frame; }
    PixelInfo pixelInfoAt(QPointF widget) const;
    QPointF frameToWidget(QPointF imagePixel) const;
    QPointF mapToFrame(QPointF widget) const;
    double zoomFactor() const { return m_zoom; }

    void clearMeasurements();
    int measurementCount() const { return int(m_distances.size() + m_angles.size()); }
    void setRois(std::vector<RoiAnnotation> rois);
    const std::vector<RoiAnnotation>& rois() const { return m_rois; }
    void clearRois();
    bool removeLastRoi();
    void addRoiForCapture(int row0, int col0, int row1, int col1);
    void refreshRoiStats();

    // Same overlay contract as the legacy canvas: the tests paint it into a
    // QImage to check contrast and clamping without a GL context.
    void paintOverlays(QPainter& painter) const;
    QRect measurementLabelRect(QPointF at, const QString& text) const;
    std::pair<QRect, QRect> overlayCornerAreas() const;
    QRect overlayLeftArea() const { return overlayCornerAreas().first; }
    QRect overlayRightArea() const { return overlayCornerAreas().second; }

signals:
    void windowChanged(double center, double width);
    void viewChanged();
    void scrollRequested(int delta);   // wheel / keys / middle-drag scrub
    void pixelInfoChanged(const PixelInfo& info);
    void roisChanged();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    enum class Drag { None, Window, Pan, Scrub, Measure, Roi };

    void syncCamera();          // m_zoom/m_pan -> the VTK camera, then render
    void rebuildDisplay();      // frame + window -> mapper input, then render
    void updateOverlay();
    double distanceMm(QPointF p1, QPointF p2, bool& spacingKnown) const;
    double angleDeg(QPointF p1, QPointF p2, QPointF p3) const;
    RoiAnnotation buildRoi(bool ellipse, int row0, int col0, int row1, int col1) const;
    void drawRois(QPainter& painter) const;
    void drawMeasurements(QPainter& painter) const;
    void drawMeasurementText(QPainter& p, QPointF at, const QString& text) const;
    QFont overlayTextFont() const;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkImageSliceMapper> m_mapper;
    vtkSmartPointer<vtkImageSlice> m_slice;
    vtkSmartPointer<vtkImageMapToColors> m_mapToColors;
    OverlayWidget* m_overlay = nullptr;

    clivus::ImageFrame m_frame;
    double m_center = 0.0;
    double m_width = 1.0;
    float m_zoom = 1.0f;
    QPointF m_pan{0, 0};
    double m_spacingRow = 0.0, m_spacingCol = 0.0;
    QString m_overlayLeft, m_overlayRight;
    ToolHint m_toolHint = ToolHint::WindowLevel;

    Drag m_drag = Drag::None;
    QPoint m_lastMouse;
    double m_scrubAccum = 0.0;
    bool m_measureDragging = false;
    QPointF m_measureStart, m_measureEnd;
    std::vector<QPointF> m_anglePending;   // Shift+clicks gathering an angle
    std::vector<DistanceMeasurement> m_distances;
    std::vector<AngleMeasurement> m_angles;
    bool m_roiDragging = false;
    QPointF m_roiStart, m_roiEnd;
    std::vector<RoiAnnotation> m_rois;
    int m_lastFrameIndex = -1;
};
