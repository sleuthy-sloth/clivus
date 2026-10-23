# Next roadmap: network access, persistent measurement, cine and reformats

Three features, chosen because each one closes a gap a radiologist hits in daily use (see the
capability comparison in the README discussion): being able to reach studies, being able to keep what
you measured, and being able to watch and reformat a stack properly.

Rules for this work, carried over from the previous roadmap:

- Core stays Qt-free; Qt lives in `app/`. No new third-party dependencies (Qt6::Network is part of Qt).
- Everything gets tests that fail without the change. Offscreen/headless tests where possible.
- No patient identifiers written to disk by these features. Server credentials go to QSettings at
  most; never to logs.
- Every phase ends with: build + `ctest`, merge to `main`, and an honest note on what was not tested.
- Windows CI must be green before any tag, and release notes must not claim Windows was launched.

## Phase D1 — DICOMweb (QIDO-RS + WADO-RS)

**Goal:** search a DICOMweb server and open a study, without a PACS-side DIMSE stack.

- `app/dicomweb_client.{h,cpp}` — Qt6::Network based:
  - `searchStudies(baseUrl, filters)` → QIDO-RS `GET /studies?PatientName=&StudyDate=&AccessionNumber=`,
    parsing the JSON array into `StudySummary { studyUid, patientName, studyDate, modalities,
    description, instances }`. `Accept: application/dicom+json`.
  - `instancesForStudy(studyUid)` → QIDO-RS `GET /studies/{uid}/instances` → instance UIDs.
  - `downloadStudy(studyUid, destDir, progress, cancel)` → WADO-RS
    `GET /studies/{uid}/series/{s}/instances/{i}` (`Accept: application/dicom`), one `.dcm` per
    instance, cancellable, reporting progress. Files land in a temp study folder under the app cache
    dir and are then opened by the normal scan path — no new decoding path.
  - Errors are explicit: HTTP status, bad JSON, partial download (count mismatch) — never a silently
    partial study.
- UI: **File ▸ Open from server…** — URL field, three filters, Search, result list
  (name · date · modality · description · instances), Download and open. URL remembered; no PHI
  persisted. Button disabled while a download runs; Cancel wired to the client's cancellation.
- Tests: `clivus_test_dicomweb` with a local `QTcpServer` mock serving canned QIDO JSON and WADO
  bodies (real corpus files) — asserts parsing, URL construction/filters, download count, cancel
  mid-download, and that a 404/500 produces a message rather than an empty list.

**Not in this phase:** C-FIND/C-MOVE (classic DIMSE), STOW-RS upload, worklists, TLS client certs,
authentication beyond a plain `Authorization` header.

## Phase D2 — Persistent annotations and ROI statistics

**Goal:** measure something, close the app, come back, and it is still there — with numbers a
clinician actually wants.

- ROI tools on the canvas: rectangle and ellipse; statistics from the *stored* (modality) values:
  mean, standard deviation, min, max, area in mm² (PixelSpacing), pixel count. Distance/angle keep
  working. One owner for state (the canvas), as today.
- Persistence: JSON sidecar per series in the app data dir, keyed by `SeriesInstanceUID` +
  slice index, storing tool type, points, and the derived numbers. Written on change (debounced),
  read on series open. Corrupt/absent sidecar = no annotations, never a crash.
- Export: CSV of all measurements for the open series (tool, slice, value, unit); a
  burn-in PNG of the current slice is a stretch goal, not a promise.
- Privacy: no patient name, no file paths in the sidecar — series UID, slice index, geometry, numbers.
- Tests: round-trip (draw → save → new window → load), corrupt sidecar, statistics against a
  synthetic frame with known values, area scaling with non-square PixelSpacing, CSV contents.

**Not in this phase:** DICOM SR/SEG interop, arrow/text labels, RECIST/Cobb templates.

## Phase D3 — Cine, trilinear reformats, slab MIP

- **Cine**: play/pause (Space in the canvas), fps control (2–30), loops the current series or
  multi-frame object through the existing decode pump (one decode at a time preserved; prefetch still
  applies). Stops on series change, cancel, or any manual slice step.
- **MPR interpolation**: trilinear as the default, nearest-neighbour selectable; the overlay says
  which is in use. Spacing-aware (mm), not index-based blending.
- **Slab MIP/MinIP** on MPR planes: thickness in mm, projected along the plane normal, computed on
  the existing volume; label shows thickness and projection.
- Tests: cine advances through slices and stops cleanly; trilinear of a linear-ramp volume equals the
  analytic value at sampled offsets (proves spacing-awareness, not index blending); MIP over a
  synthetic slab equals the max along the normal; MinIP likewise.

**Not in this phase:** oblique reformats, curved planar, volume rendering (still separate).

## Order and reporting

D1 → D2 → D3, each merged with tests green. A release (v0.2.0) once all three land, with the
validation record updated: what CI proves, what was rendered on the macOS runner, and what nobody has
yet launched on Windows.
