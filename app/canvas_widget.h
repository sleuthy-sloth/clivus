// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QOpenGLFunctions>
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
    enum class ToolHint { WindowLevel, Pan, Pointer, Measure };
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
    void setOverlay(const QString& topLeft, const QString& topRight);

signals:
    void windowChanged(double center, double width);
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
    // mm for two widget points, and the interior angle at the middle of three (degrees). Both
    // return 0 when the geometry degenerates; distance reports spacing_known = false when the
    // header gave no spacing (never invent one).
    double distanceMm(QPointF p1, QPointF p2, bool& spacingKnown) const;
    double angleDeg(QPointF p1, QPointF p2, QPointF p3) const;
    void drawMeasurements(QPainter& painter) const;
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
    enum class Drag { None, Window, Pan, Measure } m_drag = Drag::None;
    ToolHint m_toolHint = ToolHint::WindowLevel;
    double m_windowMs = 0.0;
    std::chrono::steady_clock::time_point m_lastPaint{};
};
