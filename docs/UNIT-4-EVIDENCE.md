# Unit 4 evidence — view state (part 1 of the unit)

Unit 4 as planned covers four things: view state across slices, pointer-anchored zoom, a
canvas slice-scrub gesture, and configurable mouse mappings. This file records **part 1, view state**
(`e218a18`). The other three are still open and are listed at the end so nothing looks finished that
is not.

## The defect

`CanvasWidget::setFrame()` ended with

```cpp
m_zoom = 1.0f;
m_pan = QPointF(0, 0);
```

and `MainWindow::presentSlice()` calls `setFrame()` once per slice. Every wheel step therefore
snapped the image back to fit and to centre, so zooming in to look at something and then scrolling
was impossible: the view was destroyed on the next frame. The comment above `fitView()` claimed the
opposite ("Slice changes keep zoom and pan"), which is why the review read the intent as already
implemented.

## The rule now

Zoom and pan reset only when the image itself changes:

- invalid frame — how `showSeries()` signals a series switch (`main_window.cpp:1472`),
- a frame whose width or height differs — a different matrix cannot reuse the old mapping.

Any valid frame of the same matrix keeps the current view. `MPR`'s `presentPlane()` gets the same
behaviour for free, which is what its plane scrolling wants.

## Tests

`app/test_canvas_view.cpp`, ctest target `canvas_view`:

| Check | Proves |
|---|---|
| a new image starts at 100% and unpanned | baseline |
| ctrl+wheel zooms in | the gesture under test is the real one |
| right-drag pans | same |
| stepping to the next slice keeps the zoom | the defect |
| stepping to the next slice keeps the pan | the defect |
| stepping again still keeps the zoom | a wheel run is not reset per frame |
| switching series resets the zoom and recentres | the rule still resets where it should |
| a frame with a different matrix resets the view | same |

```
$ QT_QPA_PLATFORM=offscreen ./build/linux/app/clivus_test_canvas_view
QOpenGLWidget is not supported on this platform.
canvas_view: all checks passed
```

Against the unfixed code:

```
FAIL stepping to the next slice keeps the zoom
FAIL stepping to the next slice keeps the pan
FAIL stepping again still keeps the zoom, so a wheel run is not reset every frame
```

## Lanes after the change

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --timeout 120
100% tests passed, 0 tests failed out of 25
Total Test time (real) =   5.60 sec

$ ctest --test-dir build/linux-core
100% tests passed, 0 tests failed out of 11
Total Test time (real) =   0.87 sec
```

## Still open in unit 4

- **Pointer-anchored zoom.** Ctrl+wheel zooms about the viewport centre today
  (`canvas_widget.cpp:616-625`), so the pixel under the cursor drifts. Needs the image-space point
  captured before the zoom and the pan adjusted after.
- **Canvas slice-scrub gesture.** No mouse gesture on the image changes the slice index: left drag is
  window/level or measurement, right drag pans, the wheel steps slices. Plan calls for a middle-drag
  or modifier-drag routed through the existing `goToSlice()` coalescing, with an explicit mapping
  setting (needs the owner's choice of gesture, `PLAN-EXECUTION.md` §7).
- **Configurable mouse mappings** and trackpad pixel deltas handled separately from wheel angle deltas.

## Not verified here

- How the zoom feels at the radiologist's scaling and input device. The test proves the state rule,
  not the experience.
- Windows and macOS builds.
