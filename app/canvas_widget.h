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

    void setFrame(clivus::ImageFrame frame);   // resets window to the full dynamic range
    void resetView();
    void fitView();   // zoom back to 1.0 (already fit to the viewport), keep pan and window
    void setWindowLevel(double center, double width);

signals:
    void windowChanged(double center, double width);
    void scrollRequested(int delta);          // wheel: next/previous slice
    void frameTiming(double windowMs, double paintMs, double fps);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    void uploadTexture();

    clivus::ImageFrame m_frame;
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
    enum class Drag { None, Window, Pan } m_drag = Drag::None;
    double m_windowMs = 0.0;
    std::chrono::steady_clock::time_point m_lastPaint{};
};
