// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_widget.h"

#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

CanvasWidget::CanvasWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
}

void CanvasWidget::initializeGL()
{
    initializeOpenGLFunctions();
    // Which GL implementation we actually got decides whether a Windows benchmark means anything:
    // a real driver names NVIDIA/AMD/Intel, a fallback names GDI Generic or llvmpipe.
    qInfo("GL %s | %s | %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
          reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
          reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    glGenTextures(1, &m_tex);
    // A re-created context gets a fresh, empty texture, so whatever the last allocation size was no
    // longer describes it.
    m_texWidth = 0;
    m_texHeight = 0;
    m_texChannels = 1;
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void CanvasWidget::setFrame(clivus::ImageFrame frame)
{
    m_frame = std::move(frame);
    m_frame.fullWindow(m_center, m_width);
    m_zoom = 1.0f;
    m_pan = QPointF(0, 0);
    m_texDirty = true;
    m_lastPaint = {};   // the old interval belongs to the previous image
    emit windowChanged(m_center, m_width);
    update();
}

void CanvasWidget::resetView()
{
    m_frame.fullWindow(m_center, m_width);
    m_zoom = 1.0f;
    m_pan = QPointF(0, 0);
    m_texDirty = true;
    emit windowChanged(m_center, m_width);
    update();
}

// "Fit" is the state setFrame() starts from: the image is always drawn to fit the viewport before
// zoom is applied, so fitting is just zoom back to 1.0 (and re-centre), without touching the
// window/level the user may have set.
void CanvasWidget::fitView()
{
    m_zoom = 1.0f;
    m_pan = QPointF(0, 0);
    update();
}

void CanvasWidget::setWindowLevel(double center, double width)
{
    m_center = center;
    m_width = std::max(1.0, width);
    m_texDirty = true;
    emit windowChanged(m_center, m_width);
    update();
}

void CanvasWidget::setOverlay(const QString& topLeft, const QString& topRight)
{
    m_overlayLeft = topLeft;
    m_overlayRight = topRight;
    update();
}

void CanvasWidget::uploadTexture()
{
    if (m_tex == 0 || !m_frame.valid()) return;
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // GL_LUMINANCE reads one byte per texel, GL_RGB three: the frame says which, and the byte buffer
    // render() filled already matches it.
    const GLenum format = m_frame.isColour() ? GL_RGB : GL_LUMINANCE;
    if (m_texWidth == m_frame.width && m_texHeight == m_frame.height &&
        m_texChannels == m_frame.channels) {
        // Same slice, new window: the storage is already the right size, so only the bytes move.
        // This is the drag path — every mouse move lands here, and reallocating the texture for each
        // one was pure waste.
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_frame.width, m_frame.height, format,
                        GL_UNSIGNED_BYTE, m_bytes.data());
        return;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format, m_frame.width, m_frame.height, 0, format,
                 GL_UNSIGNED_BYTE, m_bytes.data());
    m_texWidth = m_frame.width;
    m_texHeight = m_frame.height;
    m_texChannels = m_frame.channels;
}

void CanvasWidget::paintGL()
{
    QElapsedTimer timer;
    timer.start();

    m_windowMs = 0.0;
    if (m_texDirty && m_frame.valid()) {
        m_bytes.resize(m_frame.byteCount());
        m_frame.render(m_bytes.data(), m_center, m_width);
        uploadTexture();
        m_texDirty = false;
        m_windowMs = double(timer.nsecsElapsed()) / 1e6;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_frame.valid()) return;

    const double W = width();
    const double H = height();
    const double fit = std::min(W / m_frame.width, H / m_frame.height) * m_zoom;
    const double w = m_frame.width * fit;
    const double h = m_frame.height * fit;
    const double x0 = (W - w) / 2.0 + m_pan.x();
    const double y0 = (H - h) / 2.0 + m_pan.y();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, W, H, 0.0, -1.0, 1.0);   // y grows downward: image row 0 at the top
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2d(x0, y0);              // texture row 0 is t=1
    glTexCoord2f(1.0f, 1.0f); glVertex2d(x0 + w, y0);
    glTexCoord2f(1.0f, 0.0f); glVertex2d(x0 + w, y0 + h);
    glTexCoord2f(0.0f, 0.0f); glVertex2d(x0, y0 + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // Overlays live inside paintGL so they cannot sit on top of the GL surface as child widgets and
    // intercept mouse input (roadmap 2.6). Consistent font and contrast, one call, no animations.
    if (!m_overlayLeft.isEmpty() || !m_overlayRight.isEmpty()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont font = painter.font();
        font.setPointSizeF(10.0);
        painter.setFont(font);
        const QColor text(0xe6, 0xe6, 0xe6, 230);
        const QColor shadow(0, 0, 0, 160);   // enough contrast on any image, still plain
        const int pad = 8;
        if (!m_overlayLeft.isEmpty())
            painter.drawText(QRect(pad, pad, width() - 2 * pad, 200), Qt::AlignLeft | Qt::AlignTop
                                                                          | Qt::TextWordWrap,
                             m_overlayLeft);
        if (!m_overlayRight.isEmpty()) {
            const QRect bounds = painter.boundingRect(
                QRect(0, 0, width() - 2 * pad, 200), Qt::AlignRight | Qt::TextWordWrap,
                m_overlayRight);
            painter.setPen(shadow);
            painter.drawText(bounds.translated(1, 1), Qt::AlignRight, m_overlayRight);
            painter.setPen(text);
            painter.drawText(bounds, Qt::AlignRight, m_overlayRight);
        }
        painter.end();
    }

    // fps from the wall-clock interval between paints, not from this paint's CPU time:
    // the number that matters is how fast the canvas can actually present.
    const auto now = std::chrono::steady_clock::now();
    const double intervalMs = m_lastPaint.time_since_epoch().count() == 0
                                  ? 0.0
                                  : std::chrono::duration<double, std::milli>(now - m_lastPaint).count();
    m_lastPaint = now;
    emit frameTiming(m_windowMs, double(timer.nsecsElapsed()) / 1e6,
                     intervalMs > 0.0 ? 1000.0 / intervalMs : 0.0);
}

void CanvasWidget::mousePressEvent(QMouseEvent* e)
{
    m_lastMouse = e->pos();
    m_drag = (e->button() == Qt::LeftButton) ? Drag::Window : Drag::Pan;
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint d = e->pos() - m_lastMouse;
    m_lastMouse = e->pos();

    if (m_drag == Drag::Window) {
        // A colour image has no VOI transform, so there is nothing for the drag to move.
        if (m_frame.isColour()) return;
        // Horos convention: horizontal drag widens the window, vertical drag moves the centre.
        m_width = std::max(1.0, m_width * std::exp(double(d.x()) / 200.0));
        m_center -= double(d.y()) * m_width / 300.0;
        m_texDirty = true;
        emit windowChanged(m_center, m_width);
        update();
    } else if (m_drag == Drag::Pan) {
        m_pan += QPointF(d);
        emit viewChanged();
        update();
    }
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = Drag::None;
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    resetView();
}

// Keyboard slice stepping (roadmap 2.1/2.5). It reuses scrollRequested, the wheel's signal, so there is
// one navigation path and the same decode coalescing behind it. Only the canvas handles these keys, not
// the window: typing in the window/level fields must never be stolen by a global shortcut.
void CanvasWidget::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Up:
    case Qt::Key_PageUp:
        emit scrollRequested(-1);
        return;
    case Qt::Key_Down:
    case Qt::Key_PageDown:
        emit scrollRequested(1);
        return;
    default:
        break;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void CanvasWidget::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {          // ctrl+wheel zooms, like Horos
        m_zoom = std::clamp(m_zoom * (e->angleDelta().y() > 0 ? 1.1f : 1.0f / 1.1f), 0.1f, 20.0f);
        emit viewChanged();
        update();
        return;
    }
    emit scrollRequested(e->angleDelta().y() > 0 ? -1 : 1);
}
