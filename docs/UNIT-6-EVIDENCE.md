# Unit 6 — the four issues from the 0.4.0 radiologist report

Four reported behaviours were investigated against `20240e3` (the v0.4.0 tag), fixed one per commit,
and each fix carries a test that was checked to fail without it. This file is the evidence: what was
run, what it printed, and what is still not verified.

Host: Raspberry Pi 5 (aarch64, Debian 13), Qt 6.8.2, DCMTK 3.6.9, Ninja, 4 cores. Nothing here was run
on Windows or macOS, and nothing here is a claim about a GPU.

## Baseline, before any change

`cmake --preset linux && cmake --build --preset linux` completed with no errors; the suite was run
before the tree was touched:

```
96% tests passed, 1 tests failed out of 26

The following tests FAILED:
     23 - cine (Failed)
```

`clivus_test_cine` failed at `test_cine.cpp:85` (`window.currentSliceForTest() == settled`) beside
`QOpenGLWidget: Failed to create context` — a timing-sensitive test on a 4-core host with no GL. It is
pre-existing, unrelated to these four changes, and it passed on later runs (see the final run below).
Recorded here rather than hidden because a baseline that is quietly 25/26 is worth saying out loud.

## What each report turned into

| Report | Root cause found | Where |
| --- | --- | --- |
| Groups cannot be collapsed | The browser is a flat `QListWidget` whose headers were painted, never foldable; nothing tracked open/closed state | `app/main_window.cpp`, `app/browser_model.h` |
| Some on-screen text looks garbled | Both corner overlay blocks were given the same full-width rectangle and then drawn into their own measured box, so they overlap and re-wrap | `app/canvas_widget.cpp` |
| Scrolling feels jerky | Three scheduling defects: a decoded frame was presented after the wheel had left it, the prefetch and the request path decoded the same slice twice, and a step waiting on the prefetch did not advance the target | `app/main_window.cpp` |
| Window/level resets between slices | `CanvasWidget::setFrame()` re-derived the window from every incoming frame | `app/canvas_widget.cpp` |

### Bug 2, stated honestly

The report did not say which text or what it looked like. What was ruled out, with evidence in the
tree: source-encoding (MSVC compiles with `/utf-8` in all three `CMakeLists.txt`), DICOM character set
conversion (it happens once in `normaliseCharacterSet()`, and `core/tests/test_scan.cpp:348` pins a
Latin-1 object arriving as UTF-8), and font selection (the overlay follows the widget's own font,
clamped 8–16 pt). What was found and fixed is a layout defect that produces exactly the "one smear of
text" appearance in a narrow window. If the reporter's garbling is glyph-level (wrong or missing
characters rather than overlapping or clipped text), this commit does not explain it — see the last
section.

## Fails without the change, per fix

Each fix was disabled in place, the owning test target rebuilt, the test re-run, and the failing line
copied here verbatim.

**Bug 1** — `toggleGroupCollapse()` returning `false` (the 0.4.0 behaviour: nothing folds):

```
FAIL /home/spkoehl/repos/clivus-bugfix/app/test_browser_groups.cpp:187: allHidden(list, underPatient)
```

**Bug 2** — `overlayCornerAreas()` returning the 0.4.0 shared full-width rectangle:

```
FAIL the left and right overlay blocks must not overlap
FAIL the left block ends before the right one starts
```

**Bug 3** — the 0.4.0 scheduling (stale present, duplicate decode, step base, no pump after a parked
prefetch). The scroll probe's summary line, before:

```
A burst(12 steps)      requests=12 presents= 2 stale= 1 prefetch(start= 2 served= 1) ...
A presented: 1 2
FAIL /home/spkoehl/repos/clivus-bugfix/app/test_scroll.cpp:155: w.scrollStatsForTest().stalePresents == 0
```

Read that as the user experience: twelve wheel notches, the wheel ends on slice 12, and the screen
shows slice 2 — a frame decoded for an abandoned target, presented anyway, and the viewer left sitting
on it. After the fix, same code path, same machine:

```
A burst(12 steps)      requests=12 presents= 2 stale= 0 prefetch(start= 2 served= 1) ...
A presented: 1 12
```

**Bug 4** — `setFrame()` calling `fullWindow()` on every frame:

```
FAIL advancing one slice keeps the user's window/level
FAIL a slice with its own range does not re-derive the window
FAIL reversing one slice keeps the user's window/level
FAIL a frame that arrives out of order does not reset the window
```

## Final run

```
100% tests passed, 0 tests failed out of 30

Total Test time (real) =  16.87 sec
```

The four new targets are `browser_groups`, `overlay_layout`, `scroll` and `window_level`; 26 existing
tests plus 4 is the 30 above.

Scroll probe, final state of the four scenarios (Pi 5, local disk, 40-slice synthetic stack made of 40
reads of `testdata/corpus/CT_small.dcm`, no GL context):

```
A burst(12 steps)      requests=12 presents= 2 stale= 0 prefetch(start= 2 served= 1) latency p50=   3.0ms p95=   3.0ms max=   3.0ms
B prefetch overlap     requests= 2 presents= 2 stale= 0 prefetch(start= 2 served= 2) latency p50=   0.0ms p95=   0.0ms max=   0.0ms
C direction change     requests= 4 presents= 2 stale= 0 prefetch(start= 2 served= 1) latency p50=   3.0ms p95=   3.0ms max=   3.0ms
D sequential walk      requests=15 presents=15 stale= 0 prefetch(start=15 served=14) latency p50=   0.0ms p95=   1.5ms max=   1.5ms
```

`D` is the useful number: 14 of 15 steps were served from the speculative slot, so the wait between
"wheel moved" and "frame on screen" was under the 20 ms sampling granularity for the median step. These
are decode-and-schedule numbers on a Pi. They say nothing about paint cost, GPU upload or a Windows
workstation, which is the only place the reporter's complaint can actually be judged.

## Not verified

- **The reporter's machine.** No Windows hardware, no GPU. The GL path (`paintGL`, texture upload,
  `QPainter` overlays on a real context) was not exercised on any machine in this unit.
- **Their garbled text.** Not reproduced. Two questions would settle it: which text on screen, and
  whether the characters are wrong (encoding/font) or the layout is (overlap/clip). A screenshot of the
  affected area is the fastest route.
- **Their scroll feel.** The scheduling defects above are real and measured, but "smooth" on a specific
  workstation needs that workstation. A `--bench N` run there (`docs/windows.md` §0) would give the
  paint-side numbers this unit could not take.
- **Timing tests.** `cine` failed once at baseline on this host and passed afterwards. It is
  timing-sensitive; a slow runner can still make it flaky.
- **The fold gesture on macOS/Windows.** Verified through `QMouseEvent` delivery to the viewport under
  the offscreen platform, not by a human clicking on a real desktop.
