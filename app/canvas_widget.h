// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <QImage>
#include <QOpenGLWidget>
#include <QPoint>
#include <chrono>
#include <vector>

#include "clivus/image_frame.h"

// The 2D slice view: textured quad + its own window/level and pan/zoom.
// Deliberately legacy GL 2.1 (compatibility profile) - enough to measure the real
// per-frame cost now; replaced by a shader path when VTK 9 / MPR arrives.
class CanvasWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit CanvasWidget(QWidget* parent = nullptr);

    // What the pointer should say about the active tool. Hint only, with ONE deliberate
    // interaction change: while the Measure tool is active, the LEFT drag draws a measurement
    // instead of moving window/level (that is what the tool is for; the measure action says so in
    // its hint text, and switching back to any other tool restores the default gesture).
    enum class ToolHint { WindowLevel, Pan, Pointer, Measure, RoiRect, RoiEllipse };
    void setToolHint(ToolHint hint);

    // A distance is two image points; an angle is three (the second is the vertex). Points are
    // stored in WIDGET coordinates at draw time and converted to image pixels for the mm/angle
    // math, so a stored measurement survives a later zoom or pan change correct to its pixels.
    struct DistanceMeasurement {
        QPointF p1, p2;          // widget coordinates
        double distance_mm = 0.0;
        bool spacing_known = false;
    };
    struct AngleMeasurement {
        QPointF p1, p2, p3;      // widget coordinates, p2 = vertex
        double angle_deg = 0.0;
    };
    // The object's physical pixel size in mm, from the decoded frame (0 = header said nothing).
    // A region of interest, stored in IMAGE pixel coordinates (not widget ones) so it survives zoom,
    // pan and a reload from disk unchanged. Statistics are derived from the frame's modality values
    // and are recomputed on load rather than trusted from a file.
    struct RoiAnnotation {
        bool ellipse = false;
        int row0 = 0, col0 = 0, row1 = 0, col1 = 0;   // inclusive image-pixel bounds
        // Derived, never persisted:
        int pixels = 0;
        double mean = 0.0, stddev = 0.0, min = 0.0, max = 0.0, areaMm2 = 0.0;
        bool areaKnown = false;
    };
    void setRois(std::vector<RoiAnnotation> rois);   // for the slice currently shown
    const std::vector<RoiAnnotation>& rois() const { return m_rois; }
    void clearRois();
    bool removeLastRoi();
    // Recomputes the statistics of every ROI against the frame now loaded (used after a reload and
    // after a window change does not alter modality values - only after a new frame).
    void refreshRoiStats();
    // Documentation-capture helpers: frame extent, and one ROI built by the same code path a drag
    // uses (no separate statistics implementation for pictures).
    // Export helpers: the framebuffer as displayed (window/level and zoom applied, no overlay text),
    // and the window currently in use so a series export matches what the screen shows.
    QImage captureCanvasImageForExport() { return grabFramebuffer(); }
    // Draws every overlay (corner text, measurements, ROIs) with whatever painter it is given. The
    // tests paint it into a QImage to check contrast and clamping without needing a GL context.
    void paintOverlays(QPainter& painter) const;
    // Where a measurement label lands for a given anchor (clamped into the canvas). Exposed so the
    // clamping rule is testable without a GL context.
    QRect measurementLabelRect(QPointF at, const QString& text) const;
    double windowCenter() const { return m_center; }
    double windowWidth() const { return m_width; }
    int frameWidthForCapture() const { return m_frame.width; }
    int frameHeightForCapture() const { return m_frame.height; }
    void addRoiForCapture(int row0, int col0, int row1, int col1);

    void setPixelSpacing(double rowSpacingMm, double colSpacingMm);
    void clearMeasurements();
    int measurementCount() const;
    // Hairline crosshairs in image-pixel coordinates in the current view, or (-1, -1) for none.
    // Used by MPR's reference position; the canvas never computes geometry, it only draws.
    void setCrosshair(int verticalPixel, int horizontalPixel) {
        m_crosshairV = verticalPixel;
        m_crosshairH = horizontalPixel;
        update();
    }
    bool hasValidFrame() const { return m_frame.valid(); }
    int distanceCount() const { return int(m_distances.size()); }
    // Test and state introspection: the committed measurement's mm and whether the header's
    // spacing was known (unspaced objects must never carry a number).
    DistanceMeasurement distanceAt(int index) const { return m_distances.at(size_t(index)); }
    AngleMeasurement angleAt(int index) const { return m_angles.at(size_t(index)); }

    void setFrame(clivus::ImageFrame frame);   // resets window to the full dynamic range
    void resetView();
    void fitView();   // zoom back to 1.0 (already fit to the viewport), keep pan and window
    void setWindowLevel(double center, double width);
    // Overlay text the window composes; the canvas only draws it (roadmap 1.4). Empty parts are
    // simply not drawn.
    double zoomFactor() const { return m_zoom; }
    // Pan offset in widget pixels. Exposed next to zoomFactor() so the view-state rules (what a slice
    // step keeps, what a series switch resets) are testable without a GL context.
    QPointF viewPan() const { return m_pan; }
    void setOverlay(const QString& topLeft, const QString& topRight);

signals:
    void windowChanged(double center, double width);
    // A ROI was added or removed: the window persists the current slice's list.
    void roisChanged();
    void viewChanged();                        // zoom or pan moved: overlays should refresh
    void scrollRequested(int delta);          // wheel: next/previous slice
    void frameTiming(double windowMs, double paintMs, double fps);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    void uploadTexture();
    // Widget point -> image-pixel point (the inverse of the rect fit/map in paintGL).
    QPointF mapToFrame(QPointF widget) const;
    // The inverse of mapToFrame: image-pixel point -> widget point. ROIs are stored in image pixels
    // and must land correctly after any zoom or pan.
    QPointF frameToWidget(QPointF imagePixel) const;
    // mm for two widget points, and the interior angle at the middle of three (degrees). Both
    // return 0 when the geometry degenerates; distance reports spacing_known = false when the
    // header gave no spacing (never invent one).
    double distanceMm(QPointF p1, QPointF p2, bool& spacingKnown) const;
    double angleDeg(QPointF p1, QPointF p2, QPointF p3) const;
    void drawMeasurements(QPainter& painter) const;
    void drawRois(QPainter& painter) const;
    RoiAnnotation buildRoi(bool ellipse, int row0, int col0, int row1, int col1) const;
    void drawMeasurementText(QPainter& p, QPointF at, const QString& text) const;

    clivus::ImageFrame m_frame;
    // Measurement state (roadmap: measurement tools) — this canvas is the single owner. Committed
    // draws sit in m_distances / m_angles; a drag's live line stays in m_measureStart/End; an angle
    // gathers three Shift+clicks (the second click is the vertex) in m_anglePending.
    std::vector<DistanceMeasurement> m_distances;
    std::vector<AngleMeasurement> m_angles;
    QPointF m_measureStart, m_measureEnd;
    bool m_measureDragging = false;
    std::vector<QPointF> m_anglePending;   // 1 or 2 gathered clicks while an angle is in progress
    std::vector<RoiAnnotation> m_rois;     // ROIs of the slice currently shown
    QPointF m_roiStart, m_roiEnd;          // live drag, widget coordinates
    bool m_roiDragging = false;
    double m_spacingRow = 0.0, m_spacingCol = 0.0;
    int m_crosshairV = -1, m_crosshairH = -1;
    int m_lastFrameIndex = -1;   // measurements belong to one slice: cleared when the index changes
    QString m_overlayLeft, m_overlayRight;   // drawn in paintGL; never intercepts the mouse
    std::vector<uint8_t> m_bytes;
    unsigned int m_tex = 0;
    // Size the texture storage was last allocated at. A window/level change keeps it, so the update
    // is a glTexSubImage2D; a new frame with different dimensions, or a different channel count, has
    // to reallocate.
    int m_texWidth = 0;
    int m_texHeight = 0;
    int m_texChannels = 1;
    bool m_texDirty = true;
    double m_center = 0.0;
    double m_width = 1.0;
    float m_zoom = 1.0f;
    QPointF m_pan;
    QPoint m_lastMouse;
    enum class Drag { None, Window, Pan, Measure, Roi } m_drag = Drag::None;
    ToolHint m_toolHint = ToolHint::WindowLevel;
    double m_windowMs = 0.0;
    std::chrono::steady_clock::time_point m_lastPaint{};
};
