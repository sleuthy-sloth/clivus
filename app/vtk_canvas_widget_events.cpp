// VTK canvas widget, part 2: gestures and overlay drawing. The gesture
// handlers mirror the legacy canvas exactly (same drags, same signals, same
// Horos conventions); only the rendering underneath changed.

#include "vtk_canvas_widget.h"

#include "style.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QWheelEvent>

void VtkCanvasWidget::mousePressEvent(QMouseEvent* e)
{
    m_lastMouse = e->pos();
    // Middle-drag scrubs the stack: left is window/level (or the
    // measurement), right is pan, the wheel steps slices, so the remaining
    // button is the one free gesture. Steps go out through scrollRequested,
    // the same signal the wheel uses, so they inherit the existing decode
    // coalescing rather than starting a second path.
    if (e->button() == Qt::MiddleButton) {
        m_drag = Drag::Scrub;
        m_scrubAccum = 0.0;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    // Measure tool: the LEFT interaction is the measurement (the tool's
    // stated purpose); right-drag keeps panning. A Shift+click gathers angle
    // points; a plain left-drag starts a distance.
    if (m_toolHint == ToolHint::Measure && e->button() == Qt::LeftButton) {
        if (e->modifiers() & Qt::ShiftModifier) {
            if (m_measureDragging) return;   // the drag owns the gesture
            m_anglePending.push_back(QPointF(e->pos()));
            if (m_anglePending.size() == 3) {
                AngleMeasurement a;
                a.p1 = m_anglePending[0];
                a.p2 = m_anglePending[1];
                a.p3 = m_anglePending[2];
                // A repeated vertex (zero-length segment) is nothing to
                // measure: drop the gathered points without committing a
                // fake 0 degree entry. Real collinear clicks still commit:
                // their segments have length, the angle just folds to 180.
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
        updateOverlay();
        return;
    }
    // ROI tools: left-drag draws the region; right-drag still pans so a
    // mis-click never traps the view.
    if ((m_toolHint == ToolHint::RoiRect || m_toolHint == ToolHint::RoiEllipse) &&
        e->button() == Qt::LeftButton && !m_frame.isColour()) {
        m_drag = Drag::Roi;
        m_roiDragging = true;
        m_roiStart = QPointF(e->pos());
        m_roiEnd = m_roiStart;
        updateOverlay();
        return;
    }
    m_drag = (e->button() == Qt::LeftButton) ? Drag::Window : Drag::Pan;
}

void VtkCanvasWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint d = e->pos() - m_lastMouse;
    m_lastMouse = e->pos();

    // The readout follows the pointer whether or not a drag is in progress:
    // the pixel under the cursor is the question a person asks most often,
    // and it should not require a mode.
    emit pixelInfoChanged(pixelInfoAt(QPointF(e->pos())));

    if (m_drag == Drag::Scrub) {
        // Drag down = go forward through the stack, drag up = go back, at
        // one slice per 8 pixels. Sub-step movement accumulates so a slow
        // drag still steps instead of being swallowed.
        constexpr double kScrubStep = 8.0;
        m_scrubAccum += double(d.y());
        while (std::abs(m_scrubAccum) >= kScrubStep) {
            const int direction = m_scrubAccum > 0.0 ? 1 : -1;
            emit scrollRequested(direction);
            m_scrubAccum -= double(direction) * kScrubStep;
        }
        return;
    }

    if (m_drag == Drag::Measure) {
        m_measureEnd = QPointF(e->pos());
        updateOverlay();
        return;
    }
    if (m_drag == Drag::Roi) {
        m_roiEnd = QPointF(e->pos());
        updateOverlay();
        return;
    }
    if (m_drag == Drag::Window) {
        // A colour image has no VOI transform, so there is nothing for the
        // drag to move.
        if (m_frame.isColour()) return;
        // Horos convention: horizontal drag widens the window, vertical drag
        // moves the centre.
        m_width = std::max(1.0, m_width * std::exp(double(d.x()) / 200.0));
        m_center -= double(d.y()) * m_width / 300.0;
        rebuildDisplay();
        m_renderWindow->Render();
        emit windowChanged(m_center, m_width);
    } else if (m_drag == Drag::Pan) {
        m_pan += QPointF(d);
        syncCamera();
        emit viewChanged();
    }
}

void VtkCanvasWidget::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag == Drag::Scrub) {
        m_drag = Drag::None;
        m_scrubAccum = 0.0;
        setToolHint(m_toolHint);   // restore the cursor the active tool asks for
        return;
    }
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
        updateOverlay();
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
        updateOverlay();
        return;
    }
    m_drag = Drag::None;
}

void VtkCanvasWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    resetView();
}

// Keyboard slice stepping. It reuses scrollRequested, the wheel's signal,
// so there is one navigation path and the same decode coalescing behind
// it. Only the canvas handles these keys, not the window: typing in the
// window/level fields must never be stolen by a global shortcut.
void VtkCanvasWidget::keyPressEvent(QKeyEvent* e)
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
        // Reset View, same as double-click. Handled here rather than as a
        // window-level QAction shortcut so typing "R" in the window/level
        // fields is never intercepted.
        resetView();
        return;
    default:
        break;
    }
    QVTKOpenGLNativeWidget::keyPressEvent(e);
}

void VtkCanvasWidget::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {          // ctrl+wheel zooms, like Horos
        // Zoom about the pointer, not the middle of the viewport. Zooming
        // about the centre makes the thing you are looking at slide away
        // as you zoom in — you chase it with the pan instead of inspecting
        // it. Taking the image point under the cursor before the zoom and
        // panning so it lands back under the cursor afterwards is the
        // whole trick.
        const QPointF anchor = e->position();
        const QPointF imageUnderCursor = mapToFrame(anchor);
        const double before = m_zoom;
        m_zoom = std::clamp(m_zoom * (e->angleDelta().y() > 0 ? 1.1f : 1.0f / 1.1f), 0.1f, 20.0f);
        if (m_frame.valid() && m_zoom != before)
            m_pan += anchor - frameToWidget(imageUnderCursor);
        syncCamera();
        emit viewChanged();
        updateOverlay();
        return;
    }
    emit scrollRequested(e->angleDelta().y() > 0 ? -1 : 1);
}

void VtkCanvasWidget::resizeEvent(QResizeEvent* e)
{
    QVTKOpenGLNativeWidget::resizeEvent(e);
    if (m_overlay) m_overlay->setGeometry(rect());
    syncCamera();   // the fit depends on the viewport size
    updateOverlay();
}

// Overlay drawing, ported from the legacy canvas: corner blocks,
// measurements, ROIs, with the same plates, colours and clamping, so the
// two canvases never disagree about what a label looks like.
void VtkCanvasWidget::paintOverlays(QPainter& painter) const
{
    painter.setFont(overlayTextFont());
    drawMeasurements(painter);
    drawRois(painter);
    // The pen is set here, not inherited: the left block used to be drawn
    // with whatever pen the measurement pass happened to leave behind.
    const QColor text(0xe6, 0xe6, 0xe6, 230);
    const QColor shadow(0, 0, 0, 160);   // enough contrast on any image, still plain
    const auto [leftArea, rightArea] = overlayCornerAreas();
    const auto drawCorner = [&](const QString& block, Qt::Alignment align, const QRect& area) {
        if (block.isEmpty() || area.isEmpty()) return;
        // A backing plate behind the text, sized to the text and never to
        // the whole corner: light grey over a dark image (or a bright one)
        // is the other half of "the text is unreadable".
        const QRect textRect = painter.boundingRect(area, align | Qt::TextWordWrap, block);
        if (!textRect.isEmpty()) {
            painter.fillRect(textRect.adjusted(-6, -4, 6, 4), QColor(0, 0, 0, 140));
        }
        painter.save();
        painter.setPen(shadow);
        painter.translate(1, 1);
        painter.drawText(area, align | Qt::TextWordWrap, block);
        painter.restore();
        painter.setPen(text);
        painter.drawText(area, align | Qt::TextWordWrap, block);
    };
    drawCorner(m_overlayLeft, Qt::AlignLeft | Qt::AlignTop, leftArea);
    drawCorner(m_overlayRight, Qt::AlignRight | Qt::AlignTop, rightArea);
}

std::pair<QRect, QRect> VtkCanvasWidget::overlayCornerAreas() const
{
    const int pad = 8;
    const QRect viewport = rect().adjusted(pad, pad, -pad, -pad);
    if (viewport.width() <= 0 || viewport.height() <= 0) return {QRect(), QRect()};
    const int half = std::max(24, viewport.width() / 2);
    const int height = std::max(24, std::min(viewport.height(), viewport.height() / 2));
    const QRect left(viewport.left(), viewport.top(), half, height);
    const QRect right(viewport.right() - half + 1, viewport.top(), half, height);
    return {left, right};
}

void VtkCanvasWidget::drawMeasurementText(QPainter& p, QPointF at, const QString& text) const
{
    p.setFont(overlayTextFont());
    const QRect r = measurementLabelRect(at, text);
    p.fillRect(r.adjusted(-3, -3, 3, 3), QColor(0, 0, 0, 160));   // backing: readable on any image
    p.setPen(QPen(clivus::style::accent().darker(115), 1.0));
    p.drawText(r, 0, text);
}

QRect VtkCanvasWidget::measurementLabelRect(QPointF at, const QString& text) const
{
    const QFont font = overlayTextFont();
    const QRect bounds = QFontMetrics(font).boundingRect(QRect(0, 0, 1000, 1000), 0, text);
    QRect r = bounds.translated(int(at.x()) + 6, int(at.y()) - bounds.height() - 6);
    const QRect viewport = rect().adjusted(2, 2, -2, -2);
    if (r.right() > viewport.right()) r.moveRight(viewport.right());
    if (r.left() < viewport.left()) r.moveLeft(viewport.left());
    if (r.bottom() > viewport.bottom()) r.moveBottom(viewport.bottom());
    if (r.top() < viewport.top()) r.moveTop(viewport.top());
    return r;
}

void VtkCanvasWidget::drawRois(QPainter& painter) const
{
    if (m_frame.width <= 0 || m_frame.height <= 0) return;
    // Image pixels -> widget: the same fit/map the camera uses, via
    // mapToFrame inverted. Instead of inverting per point, draw in image
    // space by mapping the frame rect corners.
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

    // Live drag: outline only, no numbers until release (half a region has
    // half the statistics).
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

void VtkCanvasWidget::drawMeasurements(QPainter& painter) const
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
    // In-progress states: the live drag line, and gathered angle clicks
    // waiting for the rest.
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
