# Clivus Execution Plan (v2)

Replaces the Pi-harness handoff plan. Same intent, but with the two blockers removed, the four
wrong premises from the review corrected, and a verification contract that can actually be
enforced. Baseline: `origin/main` at `d9dfa85` (2026-10-23). Every file:line below was read at
that commit.

Scope of this plan: **Release 1** — units 1-5 plus unit 4b (overlay text, unblocked on 2026-10-24 by
the radiologist's screenshots). Units 6+ are listed at the end as a backlog with entry conditions,
not queue items.

---

## 0. Preconditions gate — do not start a unit until all four are true

### 0.1 The executor host can build and test

This Raspberry Pi **cannot** build Clivus: `cmake`, `ctest`, Qt6, and DCMTK are all absent
(`pkg-config --modversion Qt6Widgets` → none; no `/usr/include/dcmtk`). The repo's own presets are
macOS- and Windows-only. Choose the executor host explicitly:

| Lane | Preset | Prereqs | What it proves |
|---|---|---|---|
| Core only (fast) | `mac-core` | macOS, Homebrew, `brew install dcmtk`, cmake+ninja | grouping, cache, scan, decode — no Qt |
| Full app | `mac` | macOS, Homebrew, `brew install qt dcmtk` | everything incl. widgets, gestures, canvas |
| Windows | `win` | MSVC 17 2022 + `VCPKG_ROOT` set | cross-platform build + `ctest` |

Commands, verbatim from the repo:

```
# core-only lane (units 1, 3-core) — fastest feedback, no Qt build
cmake --preset mac-core && cmake --build --preset mac-core
ctest --preset mac-core --output-on-failure

# full app lane (units 2-5)
cmake --preset mac && cmake --build --preset mac
ctest --test-dir build/mac --output-on-failure

# Windows
cmake --preset win && cmake --build --preset win && ctest --preset win
```

There is **no `mac` test preset** — only `mac-core` and `win` are defined in `CMakePresets.json`.
Use `ctest --test-dir build/mac` for app tests, or add the missing test preset as part of unit 0.
Note `mac-core` sets `CLIVUS_BUILD_APP=OFF`, so Qt-free units are testable in minutes rather than
tens of minutes; use that lane wherever it applies.

CI (`windows-app.yml`) is the only Windows check available without local MSVC. It runs
`ctest --test-dir build --output-on-failure` on `windows-latest`. Pushing is forbidden by the
contract below, so CI verification happens only at the unit-5 gate, with the owner's explicit go.

### 0.2 Work off current `main`, not the checked-out branch

The working tree at `/home/spkoehl/repos/clivus` is on `parity-gate`, clean, and **72 commits
behind `origin/main`** (0 commits unique to it). A harness that reads "work against the current
repository state" will fix code that predates the async decode, export, and DICOMweb work this
plan assumes exists.

```
git fetch origin
git switch -c work/release1 origin/main      # or: git worktree add ../clivus-r1 origin/main
git rev-parse HEAD                            # must equal the base SHA recorded in 0.4
```

Untracked files survive the switch; nothing in this plan requires touching `parity-gate`.

### 0.3 Documentation reality

There is no `AGENTS.md` in the repo. The authoritative docs are `README.md` (build section),
`docs/windows.md` (Windows toolchain walkthrough), `docs/VALIDATION.md`, `docs/PRIVACY.md`,
`docs/RELEASE-CHECKLIST.md`, plus `docs/PLAN-NEXT.md` and `docs/UI-UX-ROADMAP.md` (both stale —
see 0.4).

### 0.4 Baseline record (first commit of the run, docs only)

Write `docs/BASELINE-<date>.md` containing: base SHA, host OS/CPU/GPU/driver, Qt and DCMTK
versions, the four command lines above with their real output tails, and a table of which
review findings reproduce. Explicitly confirm or refute, since the plan depends on them:

- zoom+pan reset on every slice step (unit 4 premise) — see Appendix A
- prefetch and requested decode share one worker (unit 5 premise) — see Appendix A
- cancel discards all scanned work (unit 3 premise)
- `testdata/` fixtures and `testdata/make_synthetic.py` are the only fixture source

---

## 1. Execution contract

**Per unit, in one reviewable change:**

1. State the approach and the files you expect to touch, before editing.
2. Change the code. No opportunistic refactors, no drive-by renames, no reformatting.
3. Add tests that **fail without the change**. Match the existing hand-rolled style: plain
   `int main` + `expect(bool, "what")` helpers (`core/tests/test_index_store.cpp:21`), registered
   with `add_test` in the owning `CMakeLists.txt`.
4. Run the narrowest test first, then the lane's full suite.
5. Emit the evidence block (§1.1). Then stop.

**Forbidden unless the owner says otherwise in writing:** push, force-push, PR, tag, dependency
additions, copying code from Scanthia (PolyForm Noncommercial), copying Weasis/Horos code without
a per-file header and license check, any patient-identifying data in fixtures, logs, screenshots,
caches, or commit messages, and any claim of clinical validity.

### 1.1 Evidence block — the anti-"claimed complete" rule

Two previous Clivus rounds on this project reported completion with nothing on disk. A unit is
not accepted on prose. Every unit ends with exactly this, pasted, not paraphrased:

```
UNIT <n> — <title>
base:    origin/main@<sha>        branch: <branch>        HEAD: <sha>
files:   <output of: git diff --stat origin/main...HEAD>
build:   <last 6 lines of the build command>
tests:   <verbatim ctest summary lines from the full lane run>
new:     <test name> — <what it proves> — fails without the change: yes/no
gui:     <what still needs a human/other-OS check, or "none">
```

Rules: a test line that is not pasted verbatim counts as "not run". A `yes/no` on the
fails-without-change question must come from actually reverting the fix once, or be marked
"unverified". The parent agent or the owner re-runs one command of their choosing before the next
unit starts.

### 1.2 Stop and ask (do not guess)

- Patient identity semantics: what a patient label shows, whether identifiers are ever persisted.
- Any persisted schema or on-disk format change beyond the planned `kSchemaVersion` bump.
- Any change to a measurement or calibration formula (units, spacing, angle convention).
- Anything that would require touching `parity-gate` work or another branch's files.

---

## 2. Unit 1 — Grouping identity and cache schema  (core only, S/M)

**Why.** `scanDirectory()` groups solely by `SeriesInstanceUID`: `bySeries[s.seriesUid]`
(`core/src/series_index.cpp:239`) then `if (known.files.empty()) known = s;  // first file wins the
metadata` (`:240`). Patient, study, and modality of every later file are ignored. The cache mirrors
that: `CREATE TABLE IF NOT EXISTS series(series_uid TEXT PRIMARY KEY, ...)` and
`series_file(series_uid, ordinal, ...)` (`core/src/index_store.cpp:75-80`). Separately, files with
an empty `SeriesInstanceUID` are dropped silently — `if (s.seriesUid.empty()) { it.increment(ec);
continue; }` (`core/src/series_index.cpp:227`).

**Change.**

1. Group on `(StudyInstanceUID, SeriesInstanceUID)`. Keep display metadata from the first file in
   the group, as today.
2. On a conflict inside one `(study, series)` pair — differing `PatientID`, differing
   `PatientName`, or an empty study UID where others have one — split into separate groups and
   increment a diagnostic counter. Never silently coalesce.
3. Files with no `SeriesInstanceUID`: keep them in a quarantine group keyed by source root plus a
   synthetic ordinal, and count them. Do not drop them and do not merge them into a real series.
4. Bump `kSchemaVersion` in `core/src/index_store.cpp` so old caches are a clean miss, and extend
   the series/series_file witness to the composite key. Verify the bump actually invalidates: an
   old cache file must rebuild, not load, or the fix is invisible on the owner's machine.
5. Surface the two counters (conflicts, missing-UID objects) as a non-PHI status line ("2 series
   groups split on conflicting study identity") — counts only, never paths or names.

**Fixtures.** Extend `testdata/make_synthetic.py` (stdlib-only DICOM writer, no pydicom) with
flags that emit: `--dup-series-across-studies` (same SeriesInstanceUID, two StudyInstanceUIDs, two
patient IDs), `--no-series-uid`, `--oblique` (for unit 6 sorting later). Commit the generator
change, not hand-written binaries, so fixtures stay reproducible.

**Tests.**

- `core/tests/test_scan.cpp` (existing target `scan`, takes a corpus path argument): duplicate
  series UID across studies yields two groups; same patient name with different IDs stays
  separate; conflicting patient identity inside one UID group splits and counts; missing series
  UID is retained and counted; nested roots.
- `core/tests/test_index_store.cpp` (target `index_store`): round-trip of composite keys; v2 cache
  file is treated as a miss and rebuilt; deterministic order preserved across a rebuild.

**Acceptance.** `ctest --preset mac-core -R "scan|index_store"` green, then the whole `mac-core`
suite green. No file from distinct studies can land in one series; no object disappears without a
count.

---

## 3. Unit 2 — Patient/study/series browser  (app, M)

**Why.** `scanFinished()` builds a flat `QListWidget` (`app/main_window.cpp:1084-1105`), and
`series_summary.cpp` deliberately carries no patient identity — the row tooltip is the same label
as the row (`main_window.cpp:1101`). Two patients' identically-described series are therefore
indistinguishable by design, which is the part of the radiologist's complaint that no UID fix
touches.

**Change.**

1. New `app/browser_model.{h,cpp}`: a `QAbstractItemModel` (or `QStandardItemModel`-backed tree)
   exposing Patient → Study → Series, built from `std::vector<SeriesInfo>`, no Qt widgets or
   `MainWindow` dependency, so it is unit-testable standalone.
2. Label policy (owner decision, see §7): default label is an ordinal patient label derived from
   scan order plus study date and study/series description; real patient name behind an explicit
   setting, off by default. Never in tooltips.
3. Replace the flat list construction in `scanFinished()`. Keep `applySeriesFilter()` semantics:
   filtering must not flatten patient boundaries nor select a hidden row.
4. Selection continues to drive the existing decode path unchanged — this unit rewires the
   browser only.

**Tests.** New `app/test_browser_model.cpp` linking only `browser_model.cpp` + `Qt6::Core/Gui`,
mirroring how `test_series_summary` links `series_summary.cpp` alone. Cases: hierarchy shape from
mixed-study scans, ordinal labels stable across rebuilds, filter keeps parents visible, duplicate
series descriptions under different patients stay distinct. Existing `series_filter` and
`series_summary` targets must stay green.

**Acceptance.** Selecting a series behaves exactly as before; hierarchy visible; `ctest` full
`build/mac` lane green.

---

## 4. Unit 3 — Progressive import and cancel semantics  (core + app, L)

**Why.** `startScan()` runs `scanDirectoryCached()` on the foreground pool
(`app/main_window.cpp:1055-1070`) and `scanFinished()` installs rows only after it returns
(`:1084`). Cancellation discards everything: the cancelled branch returns before touching the list
(`:1088-1094`), and `scanDirectoryCached` deliberately skips the cache write on cancel
(`core/src/index_store.cpp:451-462`). Cold import also walks the tree twice on a miss
(`fingerprintOf` in `IndexStore::open`, `core/src/index_store.cpp:244`, then `scanDirectory`), and
`loadHeader()` re-reads a whole file unbounded when the 4096-capped parse finds no Pixel Data
(`core/src/series_index.cpp:126-140`) — which is exactly the behaviour of DICOMDIR, SR, RTSTRUCT,
SEG, and presentation states. Non-DICOM files fail the capped read and stop, so they are not the
cost the review implied.

**Change.**

1. Batch API: emit complete groups as they finish, through a queued signal or a bounded
   producer/consumer, carrying a monotonically increasing folder epoch. The UI appends; the first
   usable series starts decoding before traversal ends.
2. **Cancel semantics change deliberately:** cancel stops the walk and keeps what is already
   displayed. Do not write a partial index — a partial cache reads as complete next time. State
   that decision in the commit message.
3. Epoch-guard every late batch, thumbnail, and frame from a previous folder.
4. Skip non-image SOP classes after the bounded parse instead of paying the unbounded second read;
   keep extension checks as hints only (a DICOM object without `.dcm` must still be found).
5. Replace `QtConcurrent::mapped` over every series in `startThumbnails()`
   (`app/main_window.cpp:1124-1139`) with a bounded worker count, and decode the selected series'
   first frame before queuing the rest.
6. Combine the fingerprint enumeration with the directory walk if and only if unit 0's timings
   show the second walk is material.

**Tests.** `core/tests/test_scan.cpp`: batches arrive incrementally and in completion order;
cancel after N batches keeps them; non-image object is skipped without the unbounded read (assert
on a counter, not on timing). App side (`app/test_error_paths.cpp` already exercises folder-level
failure paths): open folder B while A has batches pending, assert no A row survives.

**Acceptance.** First row and first image appear before traversal completes; cancel keeps shown
results; folder switch shows nothing stale; timings for cold and warm import recorded in the
evidence block.

---

## 5. Unit 4 — View state, zoom, and gestures  (app, M) — *unblocked half of the old Phase 3*

**Why.** Two corrections to the old plan, both verified:

1. **Zoom and pan do not survive a slice step.** `setFrame()` sets `m_zoom = 1.0f;
   m_pan = QPointF(0,0)` (`app/canvas_widget.cpp:85-86`) and `presentSlice()` calls it once per
   slice (`app/main_window.cpp:1257`). The comment claiming slices keep zoom
   (`app/canvas_widget.cpp:325-327`) is stale. This is a defect to fix, not an intent to respect.
2. **A zoom readout already exists** — `infoLine()`/status show `%` from `zoomFactor()`
   (`app/main_window.cpp:1596-1613`), and the hint bar already documents "Wheel = slices ·
   Ctrl+wheel = zoom" (`:132`). Verify and keep; do not rebuild.

**Change.**

1. Preserve zoom and pan across slices within a series; reset them on series or folder change.
   Delete or correct the stale comment.
2. Pointer-anchored zoom: compute image-space coordinates under the cursor before the zoom, adjust
   pan after so that pixel stays put. Keep Ctrl+wheel and clamp (`canvas_widget.cpp:616-625`).
3. Slice scrub gesture on the canvas (middle-drag or modifier-drag — owner decision, §7), routed
   through the existing `goToSlice()` coalescing. Add drag state to `enum class Drag`
   (`canvas_widget.h:161`) and guarantee no tool switch can strand a drag.
4. Mouse mapping settings in `app/settings_names.h` for left/middle/right/wheel plus modifiers,
   with defaults matching today's documented behaviour and a reset action.
5. Trackpad pixel deltas handled separately from wheel angle deltas (`wheelEvent` currently
   collapses every event to one slice step).

**Tests.** New or extended app test on `CanvasWidget` directly (no `MainWindow` needed for the
view-state parts): zoom survives a slice step; pointer anchor fixed through zoom in and out; min
and max clamp; middle-drag emits the scrub signal and uses coalescing; wheel without modifier still
scrolls slices.

**Acceptance.** Zoom survives scrolling; zoom is cursor-anchored; scrub works and does not steal
W/L, pan, measure, or ROI drags.

---

## 5. Unit 4b — Overlay text  (app, S/M) — *was blocked on the radiologist; no longer*

**Why.** The photos of the radiologist's screen (2026-10-24) are photographs of a monitor, angled
and cropped, so the blur in them is mostly photographic. What they do establish is *where* the text
is and *which* text is unreadable: the top-left corner overlay (series description, modality,
dimensions, slice position, orientation) drawn over the image, the diagnostic timing line under it,
and the measurement label mid-image. The measurement label reads fine; the corner overlay does not.
Reading the drawing code explains that without needing a sharper photo.

**Four defects, all verifiable in the source:**

1. **The left corner overlay is drawn with the wrong pen.** `paintGL` sets no pen before drawing
   `m_overlayLeft` (`app/canvas_widget.cpp:441-445`), so it inherits whatever the measurement pass
   left behind — and `drawMeasurements()` sets the pen unconditionally at entry
   (`app/canvas_widget.cpp:278-279`). Result: the series description and slice position are painted
   in `clivus::style::accent()`, or in the ROI stroke blue when ROIs exist
   (`:243`), instead of the intended `QColor(0xe6,0xe6,0xe6,230)`. The right overlay *is* drawn
   correctly, with a black shadow one pixel off (`:445-452`); the left one has no shadow and no
   backplate. This is the defect the photos show: coloured, thin, unshadowed text over the image.
2. **Measurement and ROI labels are not clamped to the viewport.** `drawMeasurementText` places the
   label at `(at.x + 6, at.y - height - 6)` (`app/canvas_widget.cpp:164-165`) and ROI labels at
   `rect.topLeft() + (2,-4)` (`:261`). Near the top or left edge the label is drawn outside the
   widget and clipped.
3. **Every DICOM string is decoded as UTF-8 regardless of SpecificCharacterSet.** There is no
   `SpecificCharacterSet` handling anywhere in the tree; all text goes through
   `QString::fromStdString`, which is `fromUtf8` (`app/main_window.cpp:55-56`, `:212`, `:453-455`;
   `app/series_summary.cpp`). Datasets declaring ISO_IR 100 (Latin-1) — common in older European
   exports — render accented characters as U+FFFD replacement glyphs. This is a *different* class
   from the middle-dot fix already shipped at `app/series_summary.cpp:37-40`.
4. **Fixed 10 pt, no scaling.** Overlay and measurement text are hard-coded at
   `setPointSizeF(10.0)` (`app/canvas_widget.cpp:162`, `:433`). At 4K or at 150 % scaling the corner
   block is barely legible, and the diagnostic line (`m_timing`, three numbers plus units, only when
   diagnostics are toggled — `app/main_window.cpp:1598`) lands in the same small block, which is the
   second line in the third photo.

**Change.**

1. Set the pen explicitly for `m_overlayLeft`; give it the same shadow treatment as the right
   overlay, or a single dark backplate for both. Make the two halves one code path so they cannot
   drift again.
2. Clamp label rects into `rect().adjusted(pad, pad, -pad, -pad)` inside `drawMeasurementText`, so
   every label — distance, angle, ROI stats — stays visible at any zoom, pan, or edge.
3. Resolve text through SpecificCharacterSet: read `(0008,0005)` when the object is parsed and
   convert Latin-1 / UTF-8 / ISO 2022 IR 100 accordingly, with a documented fallback. Do it once, in
   the core where the strings are read, not per call site in the UI.
4. Scale the overlay font from the widget's `devicePixelRatioF()` and a user setting, and move the
   diagnostic timing line out of the corner block into the status bar so clinical overlays never
   carry millisecond and fps numbers.

**Tests.** A headless golden-render test on `CanvasWidget` (`grabFramebuffer` on an offscreen
`QOpenGLWidget`, or a `QImage`-painted path — whichever the existing app tests can run): render a
synthetic frame dark, mid-grey, and bright, with and without a measurement, at DPR 1 and 2. Assert
pixel contrast between overlay glyphs and their immediate background exceeds a threshold, and assert
no glyph pixel falls outside the widget rect. Charset: a fixture with ISO_IR 100 and "Müller" must
render "Müller", not "M\uFFFDller".

**Acceptance.** Corner text is legible on a black image, a white image, and at the top-left of a
zoomed image; no label is ever cut by the canvas edge; the pen used by the left overlay is set
inside the paint block and cannot depend on what ran before it.

---

## 6. Unit 5 — Scroll latency: isolate speculation, then measure  (app, M)

**Why.** `pumpSlice()` and the prefetch both run on `m_foreground`, a single-thread pool
(`app/main_window.cpp:1262-1280`, comment at `:1264`). `QtConcurrent` futures cannot be cancelled:
`setFuture()` does not stop a running or queued task. So on a direction reversal the frame the
user asked for queues behind a speculative decode for the frame they no longer want. That, not the
absent LRU cache, is the mechanism worth removing first.

**Change.**

1. Move speculative decode to its own single-thread pool (or drop prefetch entirely) so a
   requested frame never waits behind speculation. Do not write `QFuture::cancel()` and call it
   done.
2. Instrument: input event → target chosen → decode queued/start/end → present → paint. Report
   median, p95, p99 per dataset and renderer.
3. Only if p95 misses the target after (1): add a bounded LRU keyed by series + frame + source
   fingerprint, with a stated byte budget and eviction rule.
4. Do not add idle-timer downscaling or interpolation tricks in this unit; measure first.

**Tests.** Deterministic reversal: prefetch in flight, target reverses, requested frame presents
first; no stale frame presented after a series change; eviction and cache hit/miss if (3) happens.
Benchmark compressed vs uncompressed synthetic stacks.

**Acceptance.** p95 improves against unit 0's baseline on the named host, with no dropped or
wrong frames and bounded memory. State the host and driver; never generalise across platforms from
one machine.

---

## 7. Gate after unit 5

Stop. Give the owner: the five evidence blocks, the p95 numbers, and a one-page list of what still
requires a human (text rendering on the radiologist's exact OS/DPI/GPU, macOS-only checks, Windows
CI result). Then the radiologist reviews, and only then is the backlog below re-prioritised.

**Open decisions to settle before unit 2** (recommendations in brackets):

1. Patient label in the browser: ordinal ("Patient A/B") with name behind a setting [recommended,
   matches the existing no-identifiers-in-tooltips stance], or name shown by default.
2. Canvas scrub gesture: middle-drag [recommended, leaves left-drag W/L and right-drag pan intact]
   or modifier+left-drag.
3. Whether a partial index is ever written on cancel [recommended: no].
4. Executor host and lane for the run (Mac `mac-core` + `mac`, or Windows `win`).

---

## 8. Backlog — not queued, each needs its own plan

Entry condition for all of these: gate §7 passed.

- **Pixel pointer, invert, geometric sorting, key images.** Small, independent, each worth one
  unit. Sorting must use orientation-normal · ImagePositionPatient, not Z, and the current default
  order (`StudyDescription` then `seriesUid`, `core/src/series_index.cpp:266-269`) is replaced by
  the hierarchy at unit 2.
- **Text/overlay defects → moved to unit 4b.** One open question remains, not blocking: the
  radiologist's exact OS, display scaling, and GPU, so the fix can be re-checked on their hardware.
  Note the series-row mojibake was already fixed at `d9dfa85` (`app/series_summary.cpp:37-40`); the
  remaining non-ASCII problem is SpecificCharacterSet, not that.
- **Layouts and linked navigation (1×1/2×1/1×2/2×2),** cross-reference lines, per-pane state.
- **ROI histogram, annotation editing/undo, CSV → DICOM SR export.**
- **MPR slabs (MIP/MinIP/mean), trilinear interpolation, PET/SUV** — only with modality-specific
  validation and a documented "why unavailable" state.

---

## Appendix A — Facts not to re-litigate

Verified against `d9dfa85`; re-check only if the base SHA moves.

| Fact | Evidence |
|---|---|
| Grouping key is SeriesInstanceUID only; first file wins metadata | `core/src/series_index.cpp:239-240` |
| Cache keys are series_uid only | `core/src/index_store.cpp:75-80` |
| Missing SeriesInstanceUID → silent drop | `core/src/series_index.cpp:227` |
| Whole list installed only after scan returns | `app/main_window.cpp:1084-1096` |
| Fingerprint walk then scan walk on miss | `core/src/index_store.cpp:244`, `:451-462` |
| Unbounded second header read for objects without Pixel Data | `core/src/series_index.cpp:126-140` |
| `setFrame()` resets zoom and pan; called per slice | `app/canvas_widget.cpp:85-86`, `app/main_window.cpp:1257` |
| Stale comment claiming slices keep zoom | `app/canvas_widget.cpp:325-327` |
| Requested decode and prefetch share one worker | `app/main_window.cpp:1262-1280` |
| Ctrl+wheel zoom 0.1-20×; plain wheel = slice; double-click = reset | `app/canvas_widget.cpp:586-589`, `:616-625` |
| No canvas gesture changes the slice index | `app/canvas_widget.cpp` mouse handlers |
| Zoom percentage readout and mouse hints already exist | `app/main_window.cpp:132`, `:1596-1613` |
| Left corner overlay drawn with an inherited pen (accent or ROI blue), no shadow | `app/canvas_widget.cpp:278-279`, `:441-445` |
| Measurement/ROI labels unclamped, clipped at canvas edges | `app/canvas_widget.cpp:164-165`, `:261` |
| No SpecificCharacterSet handling; all DICOM text decoded as UTF-8 | `app/main_window.cpp:55-56`, `:212` |
| Overlay font hard-coded 10 pt | `app/canvas_widget.cpp:162`, `:433` |
| Thumbnails: one mapped task per series, unbounded | `app/main_window.cpp:1124-1139` |
| License: GPL-3.0-or-later, SPDX headers present | `LICENSE`, `app/main_window.cpp:1` |
| Docs `PLAN-NEXT.md` / `UI-UX-ROADMAP.md` describe shipped features as future | `docs/PLAN-NEXT.md` vs `main_window.cpp:230` (DICOMweb) |

Licensing: Scanthia is PolyForm Noncommercial 1.0.0 with a required notice — design reference only,
no code copying. Weasis is EPL-2.0 OR Apache-2.0 per file. Horos is LGPL-3.0. Any adaptation needs a
per-file header check and a recorded provenance note.
