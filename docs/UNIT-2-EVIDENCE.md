# Unit 2 evidence — browser hierarchy, part A (model and labels)

Unit 2 is split in two because the widget swap in part B touches every app test that finds the series
list by type (`seriesListIn()` returns a `QListWidget*` in six test files). Part A, the grouping and
labelling, is pure logic and is what this file covers. Part B — replacing the list with a tree view —
is not done.

## What part A adds

`app/browser_model.{h,cpp}`, widgets-free:

```
Patient  →  Study  →  Series
```

- Patients are grouped by **patient ID** when the file carries one, by name only when it does not, and
  otherwise share one "unknown" node. Never by display name when an ID exists.
- Studies are grouped by **study UID**, which the scan already validated in unit 1.
- Every series keeps `seriesIndex`, its position in the scanned vector, so a selection in the tree can
  drive the existing decode path without a second lookup structure.
- Patient nodes are labelled **"Patient A", "Patient B"** by default. `BrowserOptions::showPatientNames`
  turns the name on, and the ordinal stays in the label even then, because two patients can legitimately
  share a name and the ordinal is what keeps them apart.
- Quarantined objects (no series UID, from unit 1) get their own node labelled
  "Objects without a series identity" / "No study identity", rather than being folded into a patient.
- `filterBrowserTree()` keeps parents whose descendants match, so filtering never hides the boundaries
  the hierarchy exists to show. Blank query returns everything; matching covers title, details, patient
  label and study label.

Determinism: patients are numbered by first appearance in the scan's order, so a rescan produces the
same labels.

## Tests

`app/test_browser_model.cpp`, ctest target `browser_model` — 8 blocks, all passing:

```
$ ./build/linux/app/clivus_test_browser_model
browser_model: all checks passed
```

Covered: two patient IDs make two nodes; a patient with two studies; two patients with identically
described series stay distinguishable; every series index is in range and every scanned series is
reachable exactly once; names off by default, name-plus-ordinal when on, equal names still distinct;
name-only patient fallback; unidentified series does not join a named patient; quarantine node and its
labels; filter by series title, by patient label, empty query, and a query that matches nothing.

This test has no "fails without the change" row: the file it tests did not exist before, so there is
no earlier behaviour to regress. That is different from units 1 and 4b, where the tests were written to
catch a defect that was already shipping.

## Lanes after part A

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --timeout 120
100% tests passed, 0 tests failed out of 26
Total Test time (real) =   5.41 sec
```

## Part B, what it has to do

1. Replace `QListWidget` with a tree view driven by this model (thumbs stay: `startThumbnails()` sets
   icons per series row, so the view needs a per-series icon role).
2. Update `seriesListIn()` in the app tests and every call site: `setCurrentRow`,
   `currentRowChanged` → selection model, `applySeriesFilter`, `MainWindow::showSeries(int)`.
3. Keep the behaviour part A asserts: same series count reachable, filter keeps boundaries, patient
   label policy as above.
4. Decide where the name setting lives (`app/settings_names.h`) — off by default.

Until part B lands, the shipped app still shows the flat list; nothing in the UI changed in this
commit, only the model that part B will render.
