# Clivus Release Checklist

A repeatable pre-release pass (roadmap §4). Tick through it for every tagged release.
State below reflects v0.1.1 preparation — item-by-item, with how each was checked.

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
