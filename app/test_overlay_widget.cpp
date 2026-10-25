// SPDX-License-Identifier: GPL-3.0-or-later
// The corner overlay used to be painted with QPainter inside paintGL, and on at
// least one Windows backend (0.4.1 report) individual glyphs came out sheared
// into diagonal hatching while every raster-painted widget stayed crisp. The
// overlay therefore lives in a transparent child widget now, painted with the
// raster engine. This test checks the properties that make that true without a
// GL context: the child exists, covers the canvas, takes no mouse input, and
// its own grab renders the corner text — i.e. the raster path paints it.
#include "canvas_widget.h"

#include "clivus/image_frame.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QWidget>

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

clivus::ImageFrame makeFrame(int w, int h)
{
    clivus::ImageFrame f;
    f.width = w;
    f.height = h;
    f.channels = 1;
    f.pixels.resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            f.pixels[size_t(y) * size_t(w) + size_t(x)] = float((x * 255) / (w > 1 ? w - 1 : 1));
    f.minValue = 0.0f;
    f.maxValue = 255.0f;
    f.pixelSpacingRow = 1.0;
    f.pixelSpacingCol = 1.0;
    return f;
}

// Light grey = all channels high and close together, as in test_overlay_render.
int lightGreyPixels(const QImage& img, const QRect& area)
{
    const QRect r = area.intersected(img.rect());
    int count = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor px = img.pixelColor(x, y);
            const int lo = std::min({px.red(), px.green(), px.blue()});
            const int hi = std::max({px.red(), px.green(), px.blue()});
            if (lo >= 170 && hi - lo <= 30) ++count;
        }
    }
    return count;
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
    canvas.show();
    QApplication::processEvents();

    QWidget* overlay = canvas.findChild<QWidget*>(QStringLiteral("canvasOverlay"));
    expect(overlay != nullptr, "the canvas has a canvasOverlay child");
    if (overlay == nullptr) {
        std::printf("overlay_widget: %d failure(s)\n", failures);
        return 1;
    }
    expect(overlay->parentWidget() == &canvas, "the overlay is parented to the canvas");
    expect(overlay->geometry() == canvas.rect(), "the overlay covers the canvas exactly");
    expect(overlay->testAttribute(Qt::WA_TransparentForMouseEvents),
           "the overlay is transparent to mouse events, so gestures reach the canvas");
    expect(overlay->testAttribute(Qt::WA_TranslucentBackground),
           "the overlay is translucent, so the image shows through it");
    expect(overlay->focusPolicy() == Qt::NoFocus, "the overlay never takes keyboard focus");

    {   // The overlay's own grab renders the corner text through the raster
        // engine, with no GL context involved: this is the path the reporter's
        // backend no longer touches.
        const QImage img = overlay->grab().toImage();
        expect(lightGreyPixels(img, QRect(0, 0, 240, 80)) > 40,
               "the overlay widget renders light grey text in the top-left corner");
        expect(lightGreyPixels(img, QRect(240, 0, 240, 60)) > 20,
               "the overlay widget renders light grey text in the top-right corner");
    }

    {   // No frame, no overlay: clearing the canvas must not leave stale text.
        canvas.setFrame(clivus::ImageFrame{});
        QApplication::processEvents();
        const QImage img = overlay->grab().toImage();
        expect(lightGreyPixels(img, img.rect()) == 0,
               "the overlay paints nothing once the frame is gone");
    }

    std::printf("overlay_widget: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
