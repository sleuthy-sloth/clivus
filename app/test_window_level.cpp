// SPDX-License-Identifier: GPL-3.0-or-later
// Window/level is user state (v0.4.1, radiology report: "window/level adjustments reset when advancing
// to the next image instead of persisting").
//
// setFrame() re-derived the window from every incoming frame, so every slice step — and every re-decode
// of the same slice — threw the user's adjustment away. This test pins the rule that replaced it:
//
//   - the frame's metadata default belongs to the image: applied when the canvas goes from no image to
//     an image, and at a series boundary (an invalid frame, which is what showSeries() sends);
//   - a slice of the same series keeps the user's window/level, however the frame arrived;
//   - resetView() is the explicit way back to the current frame's own default;
//   - the decoded samples themselves are never touched by any of this.
//
// No GL context is needed: setFrame(), setWindowLevel(), windowCenter()/windowWidth() and
// frameValueAtForTest() are pure state.
#include "canvas_widget.h"

#include "clivus/image_frame.h"

#include <QApplication>
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

// A frame with a gradient from lo..hi, so fullWindow() lands on a known default: centre (lo+hi)/2,
// width hi-lo.
clivus::ImageFrame makeFrame(int w, int h, float lo, float hi, int frameIndex)
{
    clivus::ImageFrame f;
    f.width = w;
    f.height = h;
    f.channels = 1;
    f.frameIndex = frameIndex;
    f.pixels.resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            f.pixels[size_t(y) * size_t(w) + size_t(x)] = lo + (hi - lo) * float(x) / float(w - 1);
    f.minValue = lo;
    f.maxValue = hi;
    f.pixelSpacingRow = 1.0;
    f.pixelSpacingCol = 1.0;
    return f;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    CanvasWidget canvas;

    // 1. The first frame establishes the default: full range of the frame.
    canvas.setFrame(makeFrame(64, 64, 0.0f, 1000.0f, 0));
    expect(near(canvas.windowCenter(), 500.0) && near(canvas.windowWidth(), 1000.0),
           "the first frame brings its own full-range default");

    // 2. The user adjusts the window, then steps one slice: the adjustment survives.
    canvas.setWindowLevel(-400.0, 400.0);
    const double sampleBefore = canvas.frameValueAtForTest(100);
    canvas.setFrame(makeFrame(64, 64, 0.0f, 1000.0f, 1));
    expect(near(canvas.windowCenter(), -400.0) && near(canvas.windowWidth(), 400.0),
           "advancing one slice keeps the user's window/level");
    expect(near(canvas.frameValueAtForTest(100), sampleBefore),
           "the frame that arrived carries the same decoded samples");

    // 3. A slice with a different range but the same matrix is still the same series: keep the window.
    canvas.setFrame(makeFrame(64, 64, -200.0f, 3000.0f, 2));
    expect(near(canvas.windowCenter(), -400.0) && near(canvas.windowWidth(), 400.0),
           "a slice with its own range does not re-derive the window");

    // 4. Reversing keeps it too (the report's other direction).
    canvas.setFrame(makeFrame(64, 64, 0.0f, 1000.0f, 1));
    expect(near(canvas.windowCenter(), -400.0) && near(canvas.windowWidth(), 400.0),
           "reversing one slice keeps the user's window/level");

    // 5. Out-of-order arrival is the same thing: a frame is a frame, and the window is not re-derived.
    canvas.setFrame(makeFrame(64, 64, 0.0f, 1000.0f, 0));
    expect(near(canvas.windowCenter(), -400.0) && near(canvas.windowWidth(), 400.0),
           "a frame that arrives out of order does not reset the window");

    // 6. An explicit reset goes back to the frame's own default (that is what the menu action and R do).
    canvas.resetView();
    expect(near(canvas.windowCenter(), 500.0) && near(canvas.windowWidth(), 1000.0),
           "reset view restores the current frame's default window");

    // 7. A series boundary (an invalid frame, which is what showSeries() sends) re-arms the default.
    canvas.setWindowLevel(40.0, 200.0);
    canvas.setFrame(clivus::ImageFrame{});
    canvas.setFrame(makeFrame(64, 64, 0.0f, 4000.0f, 0));
    expect(near(canvas.windowCenter(), 2000.0) && near(canvas.windowWidth(), 4000.0),
           "a new series starts from its own frame's default, not the previous series' window");

    // 8. A different matrix is a different image even inside one series: re-derive.
    canvas.setWindowLevel(-400.0, 400.0);
    canvas.setFrame(makeFrame(128, 128, 0.0f, 800.0f, 0));
    expect(near(canvas.windowCenter(), 400.0) && near(canvas.windowWidth(), 800.0),
           "a frame with a different matrix re-derives the window");

    // 9. Colour frames say 127.5 / 256 (render() ignores the window for them) and never carry stale
    // grayscale numbers into the fields.
    {
        std::vector<float> rgb(size_t(4) * 4 * 3, 128.0f);
        canvas.setFrame(clivus::makeColourFrame(rgb, 4, 4));
        expect(near(canvas.windowCenter(), 127.5) && near(canvas.windowWidth(), 256.0),
               "a colour frame reports the pass-through window");
    }

    if (failures == 0) std::printf("window/level: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
