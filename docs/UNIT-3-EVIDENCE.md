# Unit 3 evidence — progressive import

Two halves: the core scan API (`b6fbabb`, see the earlier note below) and the window wiring that makes
it visible to a person. This file covers the wiring.

```
UNIT 3 — progressive results in the window
base:    origin/main@d9dfa85 (via work/release1@b6fbabb)   branch: work/release1
files:   app/main_window.h/.cpp, app/test_series_filter.cpp, core/index_store.{h,cpp}
```

## What changed

1. **Folder epoch.** `MainWindow::m_folderEpoch` is bumped on every `openFolder()`. A batch carries the
   epoch it was scanned under, and `applyScanBatch()` drops anything that does not match, so a snapshot
   from a folder the user has left cannot paint over the folder they are looking at.
2. **Snapshots arrive through the event loop.** The worker builds each snapshot, copies it, and posts
   it with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`; no widget is touched off the GUI
   thread, and a `QPointer` guards the window going away underneath.
3. **One row builder.** `rebuildSeriesRows(keepUid)` now serves both the mid-scan batches and the
   finished scan, so the two cannot drift. The selection is restored by series UID, because a batch can
   add a series that sorts above the selected one.
4. **Cancel keeps results.** Cancel previously discarded everything the scan had found. It now keeps
   the batches already displayed and says how many series it found before the stop.
5. **A real bug fixed in passing:** the row index was being used as the series index. Once a filter
   hides rows those differ, so clicking the first visible row of a filtered list opened whichever series
   happened to be index 0. Each row now carries its series index in `Qt::UserRole`, and the placeholder
   row carries -1.

## Verification

```
$ cmake --build build/linux -j3                    → 0 errors
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --timeout 120
100% tests passed, 0 tests failed out of 26
Total Test time (real) =   5.44 sec
$ ctest --test-dir build/linux-core
100% tests passed, 0 tests failed out of 11
Total Test time (real) =   0.74 sec
```

`app/test_series_filter.cpp` gains a block that drives the batch entry point directly:

| Check | Proves |
|---|---|
| a batch larger than the current list replaces the rows | the populate path works |
| every row is visible and row 0 is selected | a mid-scan list is usable, not just populated |
| a batch with a stale epoch changes nothing | the folder-epoch rule |
| the filter matches rows that arrived as a batch | filtering still works on partial data |

## Two things worth recording

**The suite caught me.** The first version of `rebuildSeriesRows()` held the `QSignalBlocker` across
`setCurrentRow()`, so the initial selection never reached `showSeries()` and no first frame was decoded.
`mpr`, `annotations` and `cine` failed immediately. The blocker now covers only the row rebuild — the
exact reason it exists (suppressing `currentRowChanged(-1)` from `clear()`). Without that existing
coverage this would have shipped as "no image on open".

**What is still not covered.** The test calls `applyScanBatch()` directly; it does not exercise the
worker → queued-signal → GUI thread hop, because the committed corpus is smaller than the 64-object
progress interval and would finish before a batch could be observed. A real folder of a few thousand
objects would exercise it, and that needs a timing-tolerant test this harness has no way to write
deterministically. The rule the test does pin down is the one that can silently corrupt a view — the
stale epoch.

Also unchanged and still open from the plan: thumbnails are still one task per series on the global
pool, launched only after the scan finishes, and the progress widget is still indeterminate.
