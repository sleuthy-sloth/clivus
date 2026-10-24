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

## Part 2a — pointer-anchored zoom (shipped)

Ctrl+wheel zoomed about the viewport centre, so the thing being inspected slid out of view as you
zoomed in and you chased it with the pan. `wheelEvent` now takes the image point under the cursor
before the zoom and adds `anchor - frameToWidget(thatPoint)` to the pan after it, which puts the same
pixel back under the cursor.

`mapToFrame`/`frameToWidget` became public for this: the test drives them, and the pixel-value readout
on the roadmap needs the same mapping the measurement tools already use.

`canvas_view` gained two checks: the image point under an off-centre cursor stays under it after a
zoom, and after zooming back out it is still there. Against the centre-anchored code both fail:

```
FAIL the image point under the cursor stays under the cursor after a zoom
FAIL zooming back out keeps the same point under the cursor
```

A wrong turn worth recording: the first run of this test "passed" because the build had failed —
`mapToFrame` was private — and the stale binary ran. The suite looked green and proved nothing. The
tell was `grep -c " error|FAILED"` returning nonzero next to a passing test.

## Part 2b — middle-drag scrub (shipped, `c16f1d5`)

Left is window/level or the measurement tool, right is pan, the wheel steps slices. The middle button
was the one free gesture, and it is the one a workstation normally gives to scrolling.

Steps are emitted through `scrollRequested` — the same signal the wheel uses — so they inherit the
existing decode coalescing rather than opening a second path that could fight it. Movement accumulates
in 8-pixel steps, so a slow drag still steps instead of being swallowed, and releasing resets the
accumulator and restores the tool's cursor.

No settings key was added: this is an addition, not a remapping, and it cannot collide with an existing
gesture. Configurable mappings (swapping left and middle, say) remain open.

`canvas_view` covers: five 8-px steps forward on a 40-px drag, five back on the return, no scrubbing
after release, and left-drag still doing window/level. With the branch disabled:

```
FAIL middle-drag down steps five slices forward
FAIL dragging back up steps the same way back
```

## Still open in unit 4

- **Configurable mouse mappings.** The plan's last item: letting a user swap which button does what.
  Middle-drag scrub is now available, but the mapping itself is not user-configurable.
- **Trackpad pixel deltas** handled separately from wheel angle deltas: `wheelEvent` still collapses
  every event to one slice step.

## Not verified here

- How the zoom feels at the radiologist's scaling and input device. The test proves the state rule,
  not the experience.
- Windows and macOS builds.
