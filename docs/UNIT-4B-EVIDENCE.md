# Unit 4b evidence — overlay text

Unit 4b was split when the code was read. This file covers **4b-1: the paint defects** (done).
**4b-2, decoding DICOM text through SpecificCharacterSet**, is not done and is described at the end.

Format per `docs/PLAN-EXECUTION.md` §1.1.

```
UNIT 4b-1 — overlay contrast, label clamping, overlay font, diagnostics line
base:    origin/main@d9dfa85 (via work/release1@e839fb9)   branch: work/release1
commit:  0e5b393
files:   app/canvas_widget.cpp    | 96 ++++++++++++++++++++++++++-------------
         app/canvas_widget.h      |  8 +++
         app/main_window.cpp      |  5 +-
         app/CMakeLists.txt       |  8 +++
         app/test_overlay_render.cpp | 176 +++++++++++++++++++++++++++++++++++ (new)
```

## The two defects the report was pointing at

1. **The top-left corner block was drawn in the measurement accent colour.** `paintGL` set no pen
   before drawing `m_overlayLeft`, and `drawMeasurements()` sets the pen unconditionally at entry
   (`app/canvas_widget.cpp:278-279` before this change), so the series description, modality,
   dimensions and slice position came out in `#3d9ebe` — or the ROI stroke blue when a ROI existed —
   on a 10 pt hairline with no shadow. The right block was drawn correctly. Both now go through one
   `drawCorner` path that sets the pen explicitly.
2. **Measurement labels were never clamped.** The label rect was the anchor plus an offset, so a
   measurement near the top or left edge had its number painted outside the widget and clipped. The
   rect now comes from `measurementLabelRect()`, which clamps into `rect().adjusted(2, 2, -2, -2)`.

Also in the pass: overlay font follows the widget font (clamped 8–16 pt) instead of a fixed 10 pt,
and the diagnostics `window X ms | paint Y ms | Z fps` line moved from the corner block to the status
bar.

## How it is tested

`app/test_overlay_render.cpp`, ctest target `overlay_render`, paints `CanvasWidget::paintOverlays()`
into a `QImage` rather than taking a framebuffer grab. The offscreen platform on this host prints
`QOpenGLWidget: not supported on this platform`, so a framebuffer test would SKIP — and a test that
skips protects nothing. `paintOverlays()` was extracted from `paintGL` for exactly this reason.

Checks: top-left and top-right blocks contain light-grey text (all channels ≥ 170, spread ≤ 30) and
zero accent pixels (blue − red ≥ 40); `measurementLabelRect()` stays inside the canvas at all four
edges and does not move a label that is already inside.

```
$ QT_QPA_PLATFORM=offscreen ./build/linux/app/clivus_test_overlay_render
QOpenGLWidget is not supported on this platform.
overlay_render: all checks passed
```

## Fails without the change

Same test built against the pre-fix drawing code (pen removed, clamping removed):

```
FAIL the top-left overlay draws light grey text
FAIL the top-left overlay is not drawn in the measurement accent colour
FAIL the top-right overlay draws light grey text
FAIL the top-right overlay is not drawn in the measurement accent colour
FAIL a label near the top edge starts inside the canvas
FAIL a label at the bottom-right stays inside
FAIL the label of an edge measurement is inside the canvas
```

`the top-right overlay` fails too because removing the explicit pen removed the shadow pass as well;
on the real pre-fix code the right block was drawn correctly and only the left block was wrong.

## Full lanes after the change

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --timeout 120
100% tests passed, 0 tests failed out of 24
Total Test time (real) =   5.79 sec

$ ctest --test-dir build/linux-core
100% tests passed, 0 tests failed out of 11
Total Test time (real) =   0.70 sec
```

## Not verified here

- How it looks on the radiologist's machine: OS, font, DPI and GPU are still unknown. The pixel test
  proves the colour and the geometry, not that the result reads well at their scaling.
- Windows and macOS builds.

## 4b-2, still open

Every DICOM string is decoded with `QString::fromStdString`, which is `fromUtf8`, and nothing in the
tree reads `SpecificCharacterSet (0008,0005)`. A dataset that declares `ISO_IR 100` (Latin-1) — common
in older European exports — renders accented characters as `U+FFFD`. The fix belongs in the core where
the strings are read (`anyVRString` in `core/src/series_index.cpp`), converting Latin-1, UTF-8 and
ISO 2022 IR 100 with a documented fallback, not at each call site in the UI. It needs a fixture that
declares ISO_IR 100 and a name with an accent, and it is large enough to be its own change.
