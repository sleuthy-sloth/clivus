// SPDX-License-Identifier: GPL-3.0-or-later
// The corner overlay and the measurement labels are the text a person reads over the image, and both
// were reported as looking broken. This test paints the overlay code into a QImage instead of taking
// a framebuffer grab: the offscreen platform on a headless build host cannot reliably create a GL
// context, and a test that skips is a test that protects nothing.
//
// Two properties are checked:
//   1. the top-left block is light grey text like the top-right one, not the measurement accent colour
//      it used to inherit from whatever pen the previous drawing pass left behind;
//   2. a measurement label near an edge is clamped inside the canvas instead of being drawn outside
//      the widget and clipped away.
#include "canvas_widget.h"

#include "clivus/image_frame.h"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRect>

#include <algorithm>
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

// A frame with a real gradient, so the window/level has something to work with.
clivus::ImageFrame makeFrame(int w, int h)
{
    clivus::ImageFrame f;
    f.width = w;
    f.height = h;
    f.channels = 1;
    f.pixels.resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            f.pixels[size_t(y) * size_t(w) + size_t(x)] =
                float((x * 255) / (w > 1 ? w - 1 : 1));
    f.minValue = 0.0f;
    f.maxValue = 255.0f;
    f.pixelSpacingRow = 1.0;
    f.pixelSpacingCol = 1.0;
    return f;
}

struct Counts {
    int lightGrey = 0;   // what the corner blocks are meant to be
    int accent = 0;      // the measurement accent, which the corner blocks must not use
};

// Light grey = all channels high and close together. Accent (#3d9ebe and its darker shades) has a
// blue channel far above red, so the two are easy to tell apart.
Counts countIn(const QImage& img, const QRect& area)
{
    Counts c;
    const QRect r = area.intersected(img.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor px = img.pixelColor(x, y);
            const int lo = std::min({px.red(), px.green(), px.blue()});
            const int hi = std::max({px.red(), px.green(), px.blue()});
            if (lo >= 170 && hi - lo <= 30) {
                ++c.lightGrey;
            } else if (px.blue() - px.red() >= 40 && px.blue() >= 120) {
                ++c.accent;
            }
        }
    }
    return c;
}

QImage paintOverlaysInto(CanvasWidget& canvas, int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);   // the hardest background for light text, and the one the report showed
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    canvas.paintOverlays(painter);
    painter.end();
    return img;
}

void drag(CanvasWidget& canvas, QPoint from, QPoint to)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), canvas.mapToGlobal(from),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), canvas.mapToGlobal(to), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), canvas.mapToGlobal(to),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &release);
    QApplication::processEvents();
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    CanvasWidget canvas;
    canvas.resize(480, 360);
    canvas.setFrame(makeFrame(160, 160));
    canvas.setOverlay(QStringLiteral("MR\n160 × 160\n1 / 10"),
                      QStringLiteral("WC 400   WW 1600   100%"));

    {   // (1) Both corner blocks are light grey. The left one used to be drawn in the accent colour,
        // because paintGL never set a pen before drawing it and drawMeasurements() sets one
        // unconditionally at entry.
        const QImage img = paintOverlaysInto(canvas, 480, 360);
        const Counts left = countIn(img, QRect(0, 0, 240, 80));
        const Counts right = countIn(img, QRect(240, 0, 240, 60));
        expect(left.lightGrey > 40, "the top-left overlay draws light grey text");
        expect(left.accent == 0, "the top-left overlay is not drawn in the measurement accent colour");
        expect(right.lightGrey > 20, "the top-right overlay draws light grey text");
        expect(right.accent == 0, "the top-right overlay is not drawn in the measurement accent colour");
    }

    {   // (2) A label that would fall outside the canvas is clamped inside it. The anchors are chosen
        // so the unclamped rect is partly or wholly above/left of the widget.
        const QRect topLeft = canvas.measurementLabelRect(QPointF(4, 3), QStringLiteral("45.2 mm"));
        expect(topLeft.left() >= 0, "a label near the left edge starts inside the canvas");
        expect(topLeft.top() >= 0, "a label near the top edge starts inside the canvas");
        expect(topLeft.right() < canvas.width(), "a label near the right edge ends inside the canvas");

        const QRect bottomRight =
            canvas.measurementLabelRect(QPointF(478, 358), QStringLiteral("123.4 mm"));
        expect(bottomRight.right() < canvas.width(), "a label at the bottom-right stays inside");
        expect(bottomRight.bottom() < canvas.height(), "a label at the bottom edge stays inside");

        // And a label in open space is not moved: the clamp must only act at the edges.
        const QRect middle = canvas.measurementLabelRect(QPointF(240, 180), QStringLiteral("45.2 mm"));
        expect(middle.left() > 200 && middle.top() > 100,
               "a label in the middle of the canvas is left where it was");
    }

    {   // (3) A measurement drawn at the very top edge still paints its label: the clamped rect must be
        // inside the canvas, which is what the pixel check above cannot see once the line overlaps it.
        canvas.setToolHint(CanvasWidget::ToolHint::Measure);
        drag(canvas, QPoint(60, 4), QPoint(220, 4));
        expect(canvas.distanceCount() == 1, "the drag created one measurement");
        const QRect label = canvas.measurementLabelRect(QPointF(140, 4), QStringLiteral("160.0 mm"));
        expect(label.top() >= 0, "the label of an edge measurement is inside the canvas");
    }

    if (failures == 0) std::printf("overlay_render: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
