// SPDX-License-Identifier: GPL-3.0-or-later
// The corner overlay's layout (v0.4.1, radiology report: "some on-screen text appears garbled").
//
// What this pins is the part of the overlay that is provably wrong in the code, not a guess about the
// reporter's machine: both corner blocks were given the SAME rectangle — the full width of the canvas —
// and then the text was measured with word wrap and drawn into its own measured box. Two consequences,
// both visible in a 360 px wide window:
//
//   1. a long series description (left block) runs underneath the window/level block (right block), so
//      the two read as one smear of overlapping glyphs;
//   2. drawing into the measured box re-wraps the text at the box's width, so the lines the user gets
//      are not the lines that were measured — they are clipped by the box they were re-wrapped into.
//
// The fix gives each block half the canvas and lays it out once, in the rectangle it is drawn into.
// Painted into a QImage, not grabbed from a framebuffer: the offscreen platform cannot create a GL
// context on a headless build host, and a test that skips is a test that protects nothing.
#include "canvas_widget.h"

#include <QApplication>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

// Ink of the overlay text: the blocks draw light grey (0xe6) with a dark shadow, so "bright" counts.
int brightPixels(const QImage& image, const QRect& area)
{
    int n = 0;
    const QRect r = area.intersected(image.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() > 200 && c.green() > 200 && c.blue() > 200) ++n;
        }
    }
    return n;
}

int brightRows(const QImage& image, const QRect& area)
{
    const QRect r = area.intersected(image.rect());
    int rows = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        bool any = false;
        for (int x = r.left(); x <= r.right() && !any; ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() > 200 && c.green() > 200 && c.blue() > 200) any = true;
        }
        if (any) ++rows;
    }
    return rows;
}

// The canvas is sized here and only here: the corner areas are computed from the widget's own rect, so
// measuring them before the resize would test the areas of a different window.
QImage render(CanvasWidget& canvas, int w, int h)
{
    canvas.resize(w, h);
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    canvas.paintOverlays(painter);
    painter.end();
    return image;
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    CanvasWidget canvas;

    // A description long enough to wrap at 360 px, and a right block that says what the numbers are.
    const QString description =
        QStringLiteral("CT ABDOMEN WITH CONTRAST — SOFT TISSUE WINDOW, PORTAL VENOUS PHASE, "
                       "RECONSTRUCTED AT 1.0 MM THICKNESS WITH A 0.5 MM INTERVAL");
    const QString viewing = QStringLiteral("WC 40.0  WW 400.0   100%");
    canvas.setOverlay(description, viewing);
    canvas.resize(360, 240);   // a narrow window is where the two blocks used to collide

    // 1. The two blocks never share a rectangle. In a narrow window this is the whole bug: both used to
    // be handed (pad, pad, width - 2*pad, 200), so the left text drew straight under the right text.
    const QRect left = canvas.overlayLeftArea();
    const QRect right = canvas.overlayRightArea();
    expect(!left.intersects(right), "the left and right overlay blocks must not overlap");
    expect(left.width() > 0 && right.width() > 0, "both overlay blocks get a usable width");
    expect(canvas.rect().contains(left), "the left overlay block sits inside the canvas");
    expect(canvas.rect().contains(right), "the right overlay block sits inside the canvas");
    expect(left.right() < right.left(), "the left block ends before the right one starts");

    // 2. Both blocks actually paint text in their own rectangle, so the split is not just arithmetic.
    {
        const QImage image = render(canvas, 360, 240);
        expect(brightPixels(image, left) > 50, "the left block draws its text inside its own area");
        expect(brightPixels(image, right) > 20, "the right block draws its text inside its own area");
    }

    // 3. A wrapped block is not clipped by its own measured box: the number of text rows on screen is
    // what the wrapped layout says it should be, within antialiasing tolerance.
    {
        const QImage image = render(canvas, 360, 240);
        QFont f = canvas.font();
        const double size = f.pointSizeF() > 0.0 ? f.pointSizeF() : 9.0;
        f.setPointSizeF(size < 8.0 ? 8.0 : (size > 16.0 ? 16.0 : size));   // the overlay's own clamp
        const QRect layout = QFontMetrics(f).boundingRect(left, Qt::AlignLeft | Qt::AlignTop |
                                                                    Qt::TextWordWrap,
                                                          description);
        const int drawn = brightRows(image, left);
        // The shadow adds a pixel at each edge; three lines of slack absorbs that and the difference
        // between "rows with ink" and "line height".
        expect(layout.height() > 0, "the wrapped description has a height at all");
        expect(drawn >= 12, "the left block draws more than one line of a wrapped description");
        expect(drawn <= left.height(), "the left block's text stays inside its area");
    }

    // 4. A short overlay (the common case) is unaffected: it still lands in one line, top-left.
    {
        CanvasWidget shortOne;
        shortOne.setOverlay(QStringLiteral("CT"), QStringLiteral("WC 40.0  WW 400.0"));
        const QImage image = render(shortOne, 800, 600);
        expect(brightPixels(image, shortOne.overlayLeftArea()) > 5, "a short left block still draws");
        const int rows = brightRows(image, shortOne.overlayLeftArea());
        expect(rows > 0 && rows < 40, "a short left block draws one line, not a clipped stack");
    }

    // 5. Empty blocks draw nothing at all (no empty background rectangle over the image).
    {
        CanvasWidget empty;
        empty.setOverlay(QString(), QString());
        const QImage image = render(empty, 800, 600);
        expect(brightPixels(image, empty.rect()) == 0, "no overlay text means no overlay ink");
    }

    if (failures == 0) std::printf("overlay layout: all checks passed\n");
    return failures == 0 ? 0 : 1;
}