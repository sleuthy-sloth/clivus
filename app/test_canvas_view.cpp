// SPDX-License-Identifier: GPL-3.0-or-later
// View state across a slice step. Zoom and pan belong to the viewport, not to the slice: scrolling a
// stack with the wheel used to snap the image back to fit on every frame, because setFrame() reset
// both unconditionally while a comment three hundred lines away claimed the opposite.
//
// The rule this test pins down: a slice step keeps zoom and pan; a series switch (an invalid frame)
// or a change of matrix resets them.
#include "canvas_widget.h"

#include "clivus/image_frame.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPointF>
#include <QWheelEvent>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

clivus::ImageFrame makeFrame(int w, int h, int frameIndex)
{
    clivus::ImageFrame f;
    f.width = w;
    f.height = h;
    f.channels = 1;
    f.pixels.assign(size_t(w) * size_t(h), 100.0f);
    f.minValue = 0.0f;
    f.maxValue = 255.0f;
    f.frameIndex = frameIndex;
    f.frameCount = 20;
    f.pixelSpacingRow = 1.0;
    f.pixelSpacingCol = 1.0;
    return f;
}

// Ctrl+wheel is the zoom gesture; without the modifier the wheel steps slices.
void ctrlWheel(CanvasWidget& canvas, int notches)
{
    const QPointF at(200, 150);
    QWheelEvent event(at, canvas.mapToGlobal(at.toPoint()), QPoint(0, 0), QPoint(0, 120 * notches),
                      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&canvas, &event);
    QApplication::processEvents();
}

void rightDrag(CanvasWidget& canvas, QPoint from, QPoint to)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), canvas.mapToGlobal(from),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), canvas.mapToGlobal(to), Qt::NoButton,
                     Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), canvas.mapToGlobal(to),
                        Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &release);
    QApplication::processEvents();
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    CanvasWidget canvas;
    canvas.resize(400, 300);
    canvas.setFrame(makeFrame(160, 160, 0));

    {   // A first frame starts at fit.
        expect(std::abs(canvas.zoomFactor() - 1.0) < 1e-6, "a new image starts at 100%");
        expect(canvas.viewPan().isNull(), "a new image starts unpanned");
    }

    ctrlWheel(canvas, 2);   // zoom in twice
    rightDrag(canvas, QPoint(200, 150), QPoint(230, 170));
    const double zoom = canvas.zoomFactor();
    const QPointF pan = canvas.viewPan();
    expect(zoom > 1.0, "ctrl+wheel zooms in");
    expect(!pan.isNull(), "right-drag pans");

    {   // The defect: this used to reset to 100% and dead centre on every slice.
        canvas.setFrame(makeFrame(160, 160, 1));
        expect(std::abs(canvas.zoomFactor() - zoom) < 1e-6,
               "stepping to the next slice keeps the zoom");
        expect(canvas.viewPan() == pan, "stepping to the next slice keeps the pan");
        canvas.setFrame(makeFrame(160, 160, 2));
        expect(std::abs(canvas.zoomFactor() - zoom) < 1e-6,
               "stepping again still keeps the zoom, so a wheel run is not reset every frame");
    }

    {   // A series switch arrives as an invalid frame in showSeries().
        canvas.setFrame(clivus::ImageFrame{});
        expect(std::abs(canvas.zoomFactor() - 1.0) < 1e-6, "switching series resets the zoom");
        expect(canvas.viewPan().isNull(), "switching series recentres the image");
    }

    {   // A frame of a different matrix cannot keep the old mapping.
        canvas.setFrame(makeFrame(160, 160, 0));
        ctrlWheel(canvas, 1);
        expect(canvas.zoomFactor() > 1.0, "zoomed again before the matrix change");
        canvas.setFrame(makeFrame(128, 128, 0));
        expect(std::abs(canvas.zoomFactor() - 1.0) < 1e-6,
               "a frame with a different matrix resets the view");
    }

    if (failures == 0) std::printf("canvas_view: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
