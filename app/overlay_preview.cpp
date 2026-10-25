// SPDX-License-Identifier: GPL-3.0-or-later
// Paints the canvas' corner overlay into a PNG, with no GL context and no window: the same
// paintOverlays() the real canvas calls, drawn over a flat mid-grey "image" so text contrast and the
// backing behind the blocks can be judged instead of guessed at. Used to make the before/after images
// in docs/img/overlay-legibility-*.png and quoted in docs/UNIT-6-EVIDENCE.md.
//
// usage: clivus_overlay_preview <out.png> <width> <height> [pointSize]
#include "canvas_widget.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc < 4) {
        std::printf("usage: clivus_overlay_preview <out.png> <width> <height> [point size]\n");
        return 2;
    }
    const QString out = QString::fromLocal8Bit(argv[1]);
    const int width = std::atoi(argv[2]);
    const int height = std::atoi(argv[3]);
    // Optional fourth argument: force the widget font to a given point size first. That is how the
    // 10 pt floor is shown — on a machine whose UI font is already 10 pt or larger the floor is a
    // no-op, and claiming a difference there would be theatre.
    const double forcedPointSize = argc > 4 ? std::atof(argv[4]) : 0.0;
    if (width < 80 || height < 80) {
        std::printf("width and height must be at least 80\n");
        return 2;
    }

    CanvasWidget canvas;
    canvas.resize(width, height);
    if (forcedPointSize > 0.0) {
        QFont forced = canvas.font();
        forced.setPointSizeF(forcedPointSize);
        canvas.setFont(forced);
    }
    canvas.setOverlay(
        QStringLiteral("CT ABDOMEN WITH CONTRAST — SOFT TISSUE WINDOW, PORTAL VENOUS PHASE, "
                       "RECONSTRUCTED AT 1.0 MM"),
        QStringLiteral("WC 40.0  WW 400.0   100%"));

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    // A mid-grey luminance stands in for the anatomy: bright and dark both hide text, and a flat grey
    // shows whether the backing is doing its job without flattering either version.
    image.fill(QColor(0x9a, 0xa3, 0xab));
    QPainter painter(&image);
    canvas.paintOverlays(painter);
    painter.end();

    if (!image.save(out)) {
        std::printf("could not write %s\n", qPrintable(out));
        return 1;
    }
    std::printf("wrote %s (%dx%d)\n", qPrintable(out), width, height);
    return 0;
}
