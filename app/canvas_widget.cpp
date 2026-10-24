// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_widget.h"

#include "clivus/roi.h"

#include "style.h"

#include <QCursor>
#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

// Overlay and measurement text follow the widget's own font instead of a fixed 10 pt. On a machine
// with larger UI text, the series description and slice position were then the smallest thing on
// screen — the opposite of what they are for. Clamped so a very large system font cannot swamp the
// image it is drawn over.
QFont overlayFont(const QFont& base)
{
    QFont font = base;
    const double size = font.pointSizeF() > 0.0 ? font.pointSizeF() : 9.0;
    font.setPointSizeF(std::clamp(size, 8.0, 16.0));
    return font;
}

}  // namespace

CanvasWidget::CanvasWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setToolHint(ToolHint::WindowLevel);   // matches the default tool action the toolbar starts on
}

// Roadmap 2.6: the cursor says which tool is active. Cross for window/level, open hand for pan (the
// gesture the hand describes), plain arrow when neither is highlighted. Both drags keep working in
// every mode - the cursor is feedback, not a mode switch - which is why the tooltips spell out both
// gestures rather than pretending the inactive one is unavailable.
void CanvasWidget::setToolHint(ToolHint hint)
{
    m_toolHint = hint;
    switch (hint) {
    case ToolHint::Pan:
        setCursor(Qt::OpenHandCursor);
        break;
    case ToolHint::Pointer:
        setCursor(Qt::ArrowCursor);
        break;
    case ToolHint::Measure:
        setCursor(Qt::CrossCursor);
        break;   // crosshair says "measuring mode", same cursor as window/level
    case ToolHint::WindowLevel:
    default:
        setCursor(Qt::CrossCursor);
        break;
    }
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
    // Measurements belong to ONE slice (roadmap: measurement tools): a decode of the same frame
    // keeps them, any other slice (or series; showSeries clears as well) drops them. The spacing
    // comes with the frame, so a re-decoded slice re-states its own identity.
    const bool sameSlice = frame.valid() && m_lastFrameIndex == frame.frameIndex;
    m_lastFrameIndex = frame.frameIndex;
    // Zoom and pan are viewport state, not slice state. Stepping through a series keeps them — that is
    // what comparing slices of one stack needs, and it is what the fitView comment has always claimed
    // while the code reset both on every frame. They reset when the image itself changes shape or goes
    // away: a new series, a different matrix, a closed folder.
    const bool keepView = frame.valid() && m_frame.valid() && frame.width == m_frame.width &&
                          frame.height == m_frame.height;
    // An invalid frame (series switch) is a slice change by definition: drop the slice's output.
    setPixelSpacing(frame.pixelSpacingRow, frame.pixelSpacingCol);
    if (!sameSlice) clearMeasurements();

    m_frame = std::move(frame);
    m_frame.fullWindow(m_center, m_width);
    if (!keepView) {
        m_zoom = 1.0f;
        m_pan = QPointF(0, 0);
    }
    m_texDirty = true;
    m_lastPaint = {};   // the old interval belongs to the previous image
    emit windowChanged(m_center, m_width);
    update();
}

void CanvasWidget::setPixelSpacing(double rowSpacingMm, double colSpacingMm)
{
    m_spacingRow = rowSpacingMm;
    m_spacingCol = colSpacingMm;
    update();
}

void CanvasWidget::clearMeasurements()
{
    m_distances.clear();
    m_angles.clear();
    m_measureDragging = false;
    m_anglePending.clear();
    update();
}

int CanvasWidget::measurementCount() const
{
    return int(m_distances.size() + m_angles.size());
}

QPointF CanvasWidget::frameToWidget(QPointF imagePixel) const
{
    if (!m_frame.valid()) return QPointF();
    const double W = width(), H = height();
    const double fit = std::min(W / m_frame.width, H / m_frame.height) * m_zoom;
    const double w = m_frame.width * fit, h = m_frame.height * fit;
    const double x0 = (W - w) / 2.0 + m_pan.x();
    const double y0 = (H - h) / 2.0 + m_pan.y();
    return QPointF(imagePixel.x() * fit + x0, imagePixel.y() * fit + y0);
}

QPointF CanvasWidget::mapToFrame(QPointF widget) const
{
    if (!m_frame.valid()) return QPointF();
    const double W = width(), H = height();
    const double fit = std::min(W / m_frame.width, H / m_frame.height) * m_zoom;
    const double w = m_frame.width * fit, h = m_frame.height * fit;
    const double x0 = (W - w) / 2.0 + m_pan.x();
    const double y0 = (H - h) / 2.0 + m_pan.y();
    return QPointF(double(widget.x() - x0) / fit, double(widget.y() - y0) / fit);
}

double CanvasWidget::distanceMm(QPointF p1, QPointF p2, bool& spacingKnown) const
{
    spacingKnown = m_spacingRow > 0.0 && m_spacingCol > 0.0;
    if (!spacingKnown) return 0.0;
    const QPointF f1 = mapToFrame(p1), f2 = mapToFrame(p2);
    const double dx = (f1.x() - f2.x()) * m_spacingCol;   // columns run horizontally
    const double dy = (f1.y() - f2.y()) * m_spacingRow;   // rows run vertically
    return std::sqrt(dx * dx + dy * dy);
}

double CanvasWidget::angleDeg(QPointF p1, QPointF p2, QPointF p3) const
{
    const QPointF v1 = mapToFrame(p1) - mapToFrame(p2);
    const QPointF v2 = mapToFrame(p3) - mapToFrame(p2);
    const double d1 = std::sqrt(QPointF::dotProduct(v1, v1));
    const double d2 = std::sqrt(QPointF::dotProduct(v2, v2));
    if (d1 == 0.0 || d2 == 0.0) return 0.0;   // degenerate segment: no angle to speak of
    constexpr double kFullCircle = 360.0;
    const double deg =
        std::acos(std::clamp(QPointF::dotProduct(v1, v2) / (d1 * d2), -1.0, 1.0)) * 180.0 / M_PI;
    return deg > 180.0 ? kFullCircle - deg : deg;
}

// Every overlay the canvas draws, in one place: corner blocks, measurements, ROIs. Called from
// paintGL, and directly by the tests with a painter on a QImage, so the contrast and clamping rules
// can be checked without a GL context.
void CanvasWidget::paintOverlays(QPainter& painter) const
{
    painter.setFont(overlayFont(font()));
    drawMeasurements(painter);
    drawRois(painter);
    // The pen is set here, not inherited. The left block used to be drawn with whatever pen the
    // measurement pass happened to leave behind, so the series description came out in the
    // measurement accent colour (or the ROI stroke blue) instead of light grey, and with no shadow to
    // hold it off a bright image.
    const QColor text(0xe6, 0xe6, 0xe6, 230);
    const QColor shadow(0, 0, 0, 160);   // enough contrast on any image, still plain
    const int pad = 8;
    const auto drawCorner = [&](const QString& block, Qt::Alignment align) {
        const QRect bounds = painter.boundingRect(
            QRect(pad, pad, width() - 2 * pad, 200), align | Qt::TextWordWrap, block);
        painter.setPen(shadow);
        painter.drawText(bounds.translated(1, 1), align, block);
        painter.setPen(text);
        painter.drawText(bounds, align, block);
    };
    if (!m_overlayLeft.isEmpty()) drawCorner(m_overlayLeft, Qt::AlignLeft | Qt::AlignTop);
    if (!m_overlayRight.isEmpty()) drawCorner(m_overlayRight, Qt::AlignRight | Qt::AlignTop);
}

void CanvasWidget::drawMeasurementText(QPainter& p, QPointF at, const QString& text) const
{
    p.setFont(overlayFont(font()));
    const QRect r = measurementLabelRect(at, text);
    p.fillRect(r.adjusted(-3, -3, 3, 3), QColor(0, 0, 0, 160));   // backing: readable on any image
    p.setPen(QPen(clivus::style::accent().darker(115), 1.0));
    p.drawText(r, 0, text);
}

// Where a measurement label lands for a given anchor. Clamped into the canvas: a measurement near the
// top or left edge used to have its label drawn outside the widget, where the paint is simply clipped
// and the number disappears. Exposed so the rule can be tested without a GL context.
QRect CanvasWidget::measurementLabelRect(QPointF at, const QString& text) const
{
    const QFont font = overlayFont(this->font());
    const QRect bounds = QFontMetrics(font).boundingRect(QRect(0, 0, 1000, 1000), 0, text);
    QRect r = bounds.translated(int(at.x()) + 6, int(at.y()) - bounds.height() - 6);
    const QRect viewport = rect().adjusted(2, 2, -2, -2);
    if (r.right() > viewport.right()) r.moveRight(viewport.right());
    if (r.left() < viewport.left()) r.moveLeft(viewport.left());
    if (r.bottom() > viewport.bottom()) r.moveBottom(viewport.bottom());
    if (r.top() < viewport.top()) r.moveTop(viewport.top());
    return r;
}

CanvasWidget::RoiAnnotation CanvasWidget::buildRoi(const bool ellipse, int row0, int col0,
                                                   int row1, int col1) const
{
    RoiAnnotation roi;
    roi.ellipse = ellipse;
    if (row0 > row1) std::swap(row0, row1);
    if (col0 > col1) std::swap(col0, col1);
    roi.row0 = std::max(0, std::min(row0, m_frame.height - 1));
    roi.row1 = std::max(0, std::min(row1, m_frame.height - 1));
    roi.col0 = std::max(0, std::min(col0, m_frame.width - 1));
    roi.col1 = std::max(0, std::min(col1, m_frame.width - 1));
    if (!m_frame.valid() || m_frame.isColour()) return roi;   // colour frames carry no HU to average
    const clivus::RoiStats stats = clivus::roiStats(
        m_frame.pixels.data(), m_frame.height, m_frame.width,
        ellipse ? clivus::RoiShape::Ellipse : clivus::RoiShape::Rectangle, roi.row0, roi.col0,
        roi.row1, roi.col1, m_frame.pixelSpacingRow, m_frame.pixelSpacingCol);
    roi.pixels = stats.pixels;
    roi.mean = stats.mean;
    roi.stddev = stats.stddev;
    roi.min = stats.min;
    roi.max = stats.max;
    roi.areaMm2 = stats.areaMm2;
    roi.areaKnown = stats.areaMm2 > 0.0;
    return roi;
}

void CanvasWidget::setRois(std::vector<RoiAnnotation> rois)
{
    m_rois = std::move(rois);
    update();
}

void CanvasWidget::clearRois()
{
    if (m_rois.empty()) return;
    m_rois.clear();
    emit roisChanged();
    update();
}

bool CanvasWidget::removeLastRoi()
{
    if (m_rois.empty()) return false;
    m_rois.pop_back();
    emit roisChanged();
    update();
    return true;
}

void CanvasWidget::addRoiForCapture(const int row0, const int col0, const int row1, const int col1)
{
    m_rois.push_back(buildRoi(false, row0, col0, row1, col1));
    emit roisChanged();
    update();
}

void CanvasWidget::refreshRoiStats()
{
    if (m_rois.empty() || !m_frame.valid()) return;
    for (RoiAnnotation& roi : m_rois)
        roi = buildRoi(roi.ellipse, roi.row0, roi.col0, roi.row1, roi.col1);
    update();
}

void CanvasWidget::drawRois(QPainter& painter) const
{
    if (m_frame.width <= 0 || m_frame.height <= 0) return;
    // Image pixels -> widget: the same fit/map paintGL uses, via mapToFrame inverted. Instead of
    // inverting per point, draw in image space by mapping the frame rect corners.
    auto toWidget = [this](double col, double row) { return frameToWidget(QPointF(col, row)); };

    const QColor stroke(0x3d, 0x9e, 0xbe, 230);
    for (const RoiAnnotation& roi : m_rois) {
        const QPointF a = toWidget(roi.col0 - 0.5, roi.row0 - 0.5);
        const QPointF b = toWidget(roi.col1 + 0.5, roi.row1 + 0.5);
        QRectF rect(a, b);
        rect = rect.normalized();
        painter.setPen(QPen(stroke, 1.4));
        painter.setBrush(Qt::NoBrush);
        if (roi.ellipse)
            painter.drawEllipse(rect);
        else
            painter.drawRect(rect);
        QString text = QStringLiteral("mean %1  sd %2\nmin %3  max %4")
                           .arg(roi.mean, 0, 'f', 1)
                           .arg(roi.stddev, 0, 'f', 1)
                           .arg(roi.min, 0, 'f', 1)
                           .arg(roi.max, 0, 'f', 1);
        text += roi.areaKnown ? QStringLiteral("\narea %1 mm²").arg(roi.areaMm2, 0, 'f', 1)
                              : QStringLiteral("\narea unknown (no spacing)");
        drawMeasurementText(painter, rect.topLeft() + QPointF(2, -4), text);
    }

    // Live drag: outline only, no numbers until release (half a region has half the statistics).
    if (m_roiDragging && (m_roiEnd - m_roiStart).manhattanLength() >= 2) {
        QRectF live(m_roiStart, m_roiEnd);
        live = live.normalized();
        painter.setPen(QPen(stroke, 1.2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        const bool ellipse = m_toolHint == ToolHint::RoiEllipse;
        if (ellipse) painter.drawEllipse(live);
        else painter.drawRect(live);
    }
}

void CanvasWidget::drawMeasurements(QPainter& painter) const
{
    const QPen pen(clivus::style::accent(), 1.0);
    painter.setPen(pen);
    for (const DistanceMeasurement& d : m_distances) {
        painter.drawLine(d.p1, d.p2);
        const QPointF mid = (d.p1 + d.p2) / 2.0;
        drawMeasurementText(painter, mid,
                            d.spacing_known ? QString("%1 mm").arg(d.distance_mm, 0, 'f', 1)
                                            : QString("spacing unknown"));
    }
    for (const AngleMeasurement& a : m_angles) {
        painter.drawLine(a.p1, a.p2);
        painter.drawLine(a.p2, a.p3);
        drawMeasurementText(painter, a.p2, QString("%1\u00b0").arg(a.angle_deg, 0, 'f', 1));
    }
    // In-progress states: the live drag line, and gathered angle clicks waiting for the rest.
    if (m_measureDragging) {
        QPointF q1 = m_measureStart, q2 = m_measureEnd;
        painter.setPen(QPen(clivus::style::accent(), 1.0, Qt::DashLine));
        painter.drawLine(q1, q2);
        bool known;
        const double mm = distanceMm(q1, q2, known);
        drawMeasurementText(painter, (q1 + q2) / 2.0,
                            known ? QString("%1 mm").arg(mm, 0, 'f', 1) : QString("spacing unknown"));
    }
    if (!m_anglePending.empty()) {
        for (const QPointF& pt : m_anglePending) painter.drawEllipse(pt, 2, 2);
        if (m_anglePending.size() >= 2)
            painter.drawLine(QLineF(m_anglePending[0], m_anglePending[1]));
    }
}

void CanvasWidget::resetView()
{
    m_frame.fullWindow(m_center, m_width);
    m_zoom = 1.0f;
    m_pan = QPointF(0, 0);
    m_texDirty = true;
    emit windowChanged(m_center, m_width);
    emit viewChanged();   // zoom went back to 100%: the zoom readout has to say so
    update();
}

// Fit Image: zoom back to 1.0 (the image is always drawn fit to the viewport before zoom is
// applied) and recentre, leaving window/level alone. Reset View is the stronger version: it also
// restores the full dynamic window. Both emit viewChanged so the zoom percentage in the overlay and
// the status line is never stale after a toolbar click.
//
// Slice changes keep zoom and pan: setFrame() only resets the view when the frame is invalid or its
// matrix changes, which is what a series switch or a closed folder looks like. Stepping through the
// slices of one series therefore holds the zoom the user chose, which is what a stack comparison
// needs. (This comment described the intent for a long time while setFrame() reset both on every
// frame; the code now matches it.)
void CanvasWidget::fitView()
{
    m_zoom = 1.0f;
    m_pan = QPointF(0, 0);
    emit viewChanged();
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
    // Texture upload puts data row 0 at t=0 (no GL_UNPACK_FLIP_Y), so the screen's top edge must
    // sample t=0: image row 0 stays the top row. Swapping these two put every image bottom-up —
    // a coronal head rendered with the sinuses down (bug report, v0.1.2).
    glTexCoord2f(0.0f, 0.0f); glVertex2d(x0, y0);              // screen top = image row 0
    glTexCoord2f(1.0f, 0.0f); glVertex2d(x0 + w, y0);
    glTexCoord2f(1.0f, 1.0f); glVertex2d(x0 + w, y0 + h);
    glTexCoord2f(0.0f, 1.0f); glVertex2d(x0, y0 + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // Overlays live inside paintGL so they cannot sit on top of the GL surface as child widgets and
    // intercept mouse input (roadmap 2.6). The drawing itself lives in paintOverlays(), so the same
    // code can be painted into an image on a machine with no GL context — which is how the contrast
    // and clamping behaviour is tested.
    if (measurementCount() > 0 || m_measureDragging || !m_anglePending.empty() || !m_rois.empty() ||
        m_roiDragging || !m_overlayLeft.isEmpty() || !m_overlayRight.isEmpty()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        paintOverlays(painter);
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
    // Measure tool: the LEFT interaction is the measurement (the tool's stated purpose); right-drag
    // keeps panning. A Shift+click gathers angle points; a plain left-drag starts a distance.
    if (m_toolHint == ToolHint::Measure && e->button() == Qt::LeftButton) {
        if (e->modifiers() & Qt::ShiftModifier) {
            // Angle input: click sequence, vertex in the middle. A click while a distance drag is
            // in flight just refuses (the drag owns the gesture).
            if (m_measureDragging) return;
            m_anglePending.push_back(QPointF(e->pos()));
            if (m_anglePending.size() == 3) {
                AngleMeasurement a;
                a.p1 = m_anglePending[0];
                a.p2 = m_anglePending[1];
                a.p3 = m_anglePending[2];
                // A repeated vertex (zero-length segment) is nothing to measure: drop the
                // gathered points without committing a fake 0 degree entry. Real collinear
                // clicks still commit: their segments have length, the angle just folds to 180.
                const QPointF v1 = mapToFrame(a.p1) - mapToFrame(a.p2);
                const QPointF v2 = mapToFrame(a.p3) - mapToFrame(a.p2);
                if (QPointF::dotProduct(v1, v1) > 0.0 && QPointF::dotProduct(v2, v2) > 0.0) {
                    a.angle_deg = angleDeg(a.p1, a.p2, a.p3);
                    m_angles.push_back(a);
                }
                m_anglePending.clear();
            }
        } else {
            m_anglePending.clear();   // a new distance abandons a half-typed angle
            m_drag = Drag::Measure;
            m_measureDragging = true;
            m_measureStart = QPointF(e->pos());
            m_measureEnd = m_measureStart;
        }
        update();
        return;
    }
    // ROI tools: left-drag draws the region (the deliberate, stated interaction for these tools);
    // right-drag still pans so a mis-click never traps the view.
    if ((m_toolHint == ToolHint::RoiRect || m_toolHint == ToolHint::RoiEllipse) &&
        e->button() == Qt::LeftButton && !m_frame.isColour()) {
        m_drag = Drag::Roi;
        m_roiDragging = true;
        m_roiStart = QPointF(e->pos());
        m_roiEnd = m_roiStart;
        update();
        return;
    }
    m_drag = (e->button() == Qt::LeftButton) ? Drag::Window : Drag::Pan;
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint d = e->pos() - m_lastMouse;
    m_lastMouse = e->pos();

    if (m_drag == Drag::Measure) {
        m_measureEnd = QPointF(e->pos());
        update();
        return;
    }
    if (m_drag == Drag::Roi) {
        m_roiEnd = QPointF(e->pos());
        update();
        return;
    }
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
    if (m_drag == Drag::Roi) {
        m_drag = Drag::None;
        m_roiDragging = false;
        const QPointF a = mapToFrame(m_roiStart);
        const QPointF b = mapToFrame(m_roiEnd);
        // A drag shorter than a couple of image pixels is a click, not a region.
        if (std::abs(b.x() - a.x()) >= 2.0 && std::abs(b.y() - a.y()) >= 2.0) {
            m_rois.push_back(buildRoi(m_toolHint == ToolHint::RoiEllipse, int(std::lround(a.y())),
                                      int(std::lround(a.x())), int(std::lround(b.y())),
                                      int(std::lround(b.x()))));
            emit roisChanged();
        }
        update();
        return;
    }
    if (m_drag == Drag::Measure) {
        m_drag = Drag::None;
        const bool meaningful = (m_measureEnd - m_measureStart).manhattanLength() >= 2;
        bool known;
        distanceMm(m_measureStart, m_measureEnd, known);
        if (m_measureDragging && meaningful) {
            DistanceMeasurement d;
            d.p1 = m_measureStart;
            d.p2 = m_measureEnd;
            d.spacing_known = known;
            d.distance_mm = distanceMm(d.p1, d.p2, known);
            m_distances.push_back(d);
        }
        m_measureDragging = false;
        update();
        return;
    }
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
    case Qt::Key_R:
        // Reset View, same as double-click. Handled here rather than as a window-level QAction
        // shortcut so typing "R" in the window/level fields is never intercepted (roadmap 2.1).
        resetView();
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
