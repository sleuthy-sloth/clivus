# Clivus Release Checklist

A repeatable pre-release pass (roadmap §4). Tick through it for every tagged release.
State below reflects preparation through **v0.4.1** — item-by-item, with how each was checked.

## 0a. v0.4.1 additions — the four issues from the 0.4.0 report

Full evidence, including the verbatim "fails without the change" lines and the scroll numbers:
[UNIT-6-EVIDENCE.md](UNIT-6-EVIDENCE.md).

- [x] Browser groups fold and unfold (reported: a large folder could not be collapsed) — new `browser_groups`
      target: click and keyboard folding at both levels, nested state kept per group, a query revealing
      matches inside a folded group and restoring the folds on clear, a scan batch keeping both the fold
      state and the selection, an empty folder. Fold keys are patient key + study UID, never row numbers.
- [x] Overlay text legible (reported: "some on-screen text appears garbled") — `overlay_layout`: the two
      corner blocks cannot overlap, a wrapped block is not clipped by its own measured box, the font floor
      is 10 pt, and each block has a backing plate sized to its text. Contrast went 1.84:1 → 6.09:1 on a
      mid-grey background (`docs/img/overlay-legibility-*.png`, painted by the real `paintOverlays()`).
      **The reported symptom itself was not reproduced**; see the open question in UNIT-6-EVIDENCE.md.
- [x] Stack scrolling (reported: jerky) — new `scroll` target: a 12-step burst presents only the target that
      was real when it arrived plus the final one (before: it presented an abandoned slice and stayed on
      it), a step into a slice the prefetch is decoding lands once, a direction change ends on the last
      request. Request→present numbers are printed by the probe; they are Pi numbers, not a GPU claim.
- [x] Window/level persists across slices (reported: reset on the next image) — new `window_level` target:
      keep on step forward/back/out-of-order, reset only at a series boundary or a different matrix,
      `resetView()` still restores the frame's default, colour frames unchanged, samples untouched.
- [x] No regression in the existing 26 targets — `ctest` 30/30 on this host (baseline was 25/26: `cine`,
      timing-sensitive on 4 cores, failed once before any change and passed afterwards).

## 0. v0.4.0 additions — checked, and how

Every item below has a test target that fails without the change; see the matching
`docs/UNIT-*-EVIDENCE.md` for the verbatim runs and the revert checks.

- [x] Series grouped by (study, series), not by series UID alone — `scan`: one UID under two studies is
      two series, one UID with two patient IDs splits into two, and a snapshot never merges them.
- [x] Objects with no SeriesInstanceUID are kept and counted, not dropped — `scan` (quarantine group,
      `ScanDiagnostics::quarantinedObjects`). The committed corpus contains two such JPEG-LS objects;
      they were invisible before and are reachable now.
- [x] Cache schema v5 with a composite key, and an older cache is a clean miss — `index_store` ages a
      cache to version 4 and requires a rebuild. This also fixed a real defect: a version bump used to
      drop the tables *after* creating them, so the first save after an upgrade failed.
- [x] Browser shows patient and study boundaries — `series_filter` (headers present, header opens its
      first series, a filter keeps headers with their children, a no-match filter leaves none).
      `browser_model` covers the grouping and label rules without a window.
- [x] Series appear while the scan is still running — `scan` (progress snapshots are partial and never
      overshoot) and `series_filter` (a batch populates the list, a stale-epoch batch changes nothing).
      The worker → UI hop is not exercised by a test: the corpus is smaller than the batch interval.
- [x] Cancel keeps the series already found — code path in `scanFinished`; exercised indirectly by
      `error_paths`. Not separately asserted.
- [x] Zoom about the pointer, zoom/pan surviving slice steps — `canvas_view` (anchor holds through zoom
      in and out; a slice step keeps both; a series switch resets both).
- [x] Middle-drag scrubs slices through the coalescing path — `canvas_view` (five steps forward, five
      back, nothing after release, left-drag still window/level).
- [x] Overlay text contrast, label clamping, declared charset — `overlay_render` (light grey corner
      text, no accent bleed, labels clamped at every edge) and `scan` (Latin-1 name arrives as UTF-8).
- [x] Pixel readout claims only what the file supports — `canvas_view` (1-based row/column, frame's own
      sample, no unit without a rescale pair) and `scan` (CT → HU, MR → none).
- [x] Speculative decode no longer queues ahead of a requested frame — no test; a single-thread pool was
      added beside the foreground pool. The measurement that would prove the effect (slice-step
      latency, p95) needs a machine with a GL context and has **not** been run. Do not claim otherwise.

Validation scope lives in [VALIDATION.md](VALIDATION.md): CI builds and packages Windows; nobody has
launched the packaged exe on Windows hardware yet, and this checklist must not imply otherwise.

## 1. Functional regression — folder and study handling

- [x] Open a valid folder — covered continuously by the hash-based scan/index test (`index_store`),
      and interactively exercised offline (manual chapter `Opening DICOM Files` documents the flow).
- [x] Open an empty folder — empty-folder error state covered interactively: the status message is
      explicit ("No DICOM images were found — check the folder …"), progress and cancel clear, and
      the app stays usable. Driven by the error-path harness (`clivus_test_error_paths`, scenario A).
- [x] Open a folder containing non-DICOM files — the scan skips objects without DICOM pixel data
      rather than failing; verified by the same harness run against a partially-poisoned copy
      (scenario B uses an mtime-restored, in-place-corrupted copy so the failure happens at
      decode time, not scan time).
- [x] Open an inaccessible folder — the scan errors out safely without crashing the loop;
      verified at harness level with a missing-file injection (scenario B) and by inspection of
      `scanDirectoryCached`'s skip-permission-denied path.
- [x] Cancel a scan — modelled as a first-class path: status returns to Ready, no stuck progress
      bar, queued thumbnails are dropped, in-flight slice decodes are dropped by `m_epoch` check.
      The cancel semantics are codified in `MainWindow::cancelLoading` (roadmap 3.5).
- [x] Reopen another folder after an error — no restart needed; `openFolder` resets the study
      state and `m_epoch` retires any in-flight work. Verified by reopening the pristine corpus
      after failures (`test_error_paths` scenario D).

## 2. Functional regression — series and image handling

- [x] Multi-series study, per-series selection — the series filter harness (`series_filter`) drives
      real selection through the scan path; 5-series corpus.
- [x] Single-image series — handled identically to a one-slice multi-frame stack; the slice bar
      shows/`Single image`, navigation is disabled. Covered by the `series_filter` harness.
- [x] Missing optional metadata — series rows show explicit fallbacks ("(no description)", and the
      options bar / overlays omit unavailable fields rather than filling them in). Verified in the
      harness against a synthetic row with an empty description.
- [x] Failed thumbnail — verified: image-missing icon, plain status message (no DCMTK text leaks),
      and the app stays usable (roadmap 1.6/1.3). Codified in `fillReadyThumbnails`.
- [x] A slice that cannot be decoded — verified: readable status message, decode continues on
      other slices; failed slices are never auto-retried (`m_failedSlice`).
- [x] Series switching during background work — `m_epoch` bumps prevent a stale decode landing on
      the newly selected series. Rust code reading + full ctest; stale-result protection is
      covered by `test_series_filter`'s async-selection check.
- [x] Rapid slice scrolling — the decode pump coalesces (one decode in flight, chasing
      `m_desiredSlice`); a late result for an already-passed slice is discarded. Covered by
      `pumpSlice`/`sliceFinished` code bookkeeping reviewed during Phase 2 + `test_series_filter`.

## 3. Functional regression — image controls

- [x] Fit Image (F), Reset View (double-click/R), pan, zoom (Ctrl+wheel) — behavior reviewed and
      locked in Phase 1/2; no rendering-equation changes since (`image_frame.h` untouched).
- [x] Window/level via mouse (left-drag), numeric entry + validation — Phase 2 followed by the
      offscreen harness: WC/WW fields live-sync with drag, invalid values revert.
- [x] Presets — Soft Tissue/Lung/Bone/Brain/Liver/MRI default, disabled on colour frames, with
      explicit `(custom)` state. Verified in the offscreen harness (`view_controls`). Values are
      documented in the manual and in comments near the preset table.
- [x] Colour behaviour — colour frames render without windowing; the WC/WW fields disable with an
      explanatory note (roadmap 2.2.3). Verified in test's photometric/colour suites, plus the UI
      harness.
- [x] Slice navigation — wheel, slider and keyboard all route through the same decode channel;
      the slider reflects what the canvas actually delivers.

## 4. Application state

- [x] Resize, collapse/expand sidebar — splitter behaviour reviewed, with `m_splitter` state
      persisted and restored (no degenerate offscreen bindings).
- [x] Restart and verify saved preferences — settings roundtrip harness (`settings_roundtrip`).
- [x] Open/close the in-app Help and About — verified interactively (the Help window is a native
      QDialog; no network access, no external resources).
- [x] Use every documented keyboard shortcut — verified against `main_window.cpp` and
      matching manual table (`Keyboard and Mouse Controls`). The deliberate absence of a
      window-scope shortcut for **Reset View** (the canvas owns `R`) is documented in the roadmap
      and in code.
- [x] Cancel and Exit behave correctly during supported operations — cancel is a first-class
      path; Exit at any moment saves layout only.

## 5. Visual regression

Slides reviewed at synthetic window sizes where the Pi permits (offscreen render):
- [x] Empty state (welcome page on open)
- [x] A folder being scanned (thumb progress, explicit Cancel visible)
- [x] A loaded study with overlays and the slice bar
- [x] A search result / no results state
- [ ] A long-series with a long description: layout is wrapped in `QListWidget::setWordWrap(true)`;
      visual check pending on Windows/Mac release build (real WAV/ANGLE hardware).
- [x] Error dialogs — status text used consistently instead of modals for recoverable issues.

Not verifiable on this Pi: real human-eye evaluation of the dark theme, high-DPI behavior, and
Windows-specific text clipping (roadmap 3.7 / Phase 4 §4.2). This item is a standing pass/fail
open question until a Windows desktop review, and is the reason the checklist is re-run per
release rather than considered "done".

## 6. Documentation and release review

- [x] In-app User Manual complete (12 chapters, searchable, offline-first, no external deps).
- [x] Shortcuts verified against the implementation (the manual + code mentioned above).
- [x] Menu entries verified against the implementation.
- [x] Supported formats and known limitations documented accurately (PALETTE COLOR,
      non-linear LUT Sequences are the explicit gaps).
- [x] Research-use limitation is visible in the manual, in the About dialog, and in the README.
- [x] README feature list matches the application state.
- [x] Third-party licences: LICENSE + THIRD-PARTY.md ship in the release artifact (GPL-3.0).
- [x] Raw error text does not surface as the only user-facing explanation.

## Known gaps that are openly deferred

- **Real-machine visual/interaction review** (Windows/Mac): the one true human-eyes pass — dark
  theme, high-DPI rendering, gesture feel on ANGLE vs Apple GPU. Not checkable from this Pi.
- **Recent folders** (roadmap 3.3): deliberately deferred; the privacy decision not to persist
  paths is recorded in code.
- **PALETTE COLOR / non-linear LUT Sequences**: decode-as-stored; the limitation is documented in
  code + manual, not hidden.

## 7. Privacy, cache and manual (added with the v0.1.5 pass)

- [x] Cache lives outside the DICOM folder — `index_store` test asserts the path is under
      `CLIVUS_CACHE_DIR`, that the filename does not contain the source path, and that a legacy
      in-folder index is deleted on open.
- [x] No patient names in the cache — the same test reads the database bytes and rejects
      `Doe^Jane`; schema v4 has no `patient_name` column; `docs/PRIVACY.md` documents the fields.
- [x] Cache failures degrade, never block — corrupt file, unwritable cache directory, and
      save-without-database are covered by tests; the viewer scans uncached in every case.
- [x] Manual search finds article text — `help_search` (window) and `manual_search` (logic) cover
      body-only phrases, case-insensitivity, whitespace-only queries, no-results, and clearing.
- [x] Manual describes the current feature set — measurements and MPR are documented as implemented
      (MPR as experimental); the stale "not supported" paragraph is gone.
- [x] Screenshots are real — produced by the app (`--capture`) on a runner with a window server;
      concept boards are labelled as concepts, never as screenshots.
- [ ] Windows packaged-app smoke test — **not run**; the exact steps are in VALIDATION.md §
      "remaining validation steps".
- [ ] High-DPI / multi-monitor / GPU matrix — **not run**; needs desktop hardware.
- [ ] Large-study performance — **not run**; the committed corpus is synthetic and small.
