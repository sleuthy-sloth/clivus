# Unit 1 evidence — grouping identity and cache schema

Recorded in the format `docs/PLAN-EXECUTION.md` §1.1 requires. Pasted output, not a summary.

```
UNIT 1 — patient/study/series grouping identity and cache schema
base:    origin/main@d9dfa85      branch: work/u1 → merged to work/release1
commits: ab223ac  e33a728  11b2d01
files:   core/include/clivus/index_store.h        |  5 +-
         core/include/clivus/series_index.h       | 17 +++-
         core/src/index_store.cpp                 | 141 ++++++++++++++++--------
         core/src/series_index.cpp                | 193 ++++++++++++++++++++++++------
         core/tests/test_index_store.cpp          |  58 +++++++
         core/tests/test_scan.cpp                 | 160 ++++++++++++++++++++++
         core/CMakeLists.txt                      |   3 +-
         7 files changed, 473 insertions(+), 107 deletions(-)
```

## Build

```
$ cmake --build build/linux-core -j3
(no output: no errors, no failures)

$ cmake --build build/linux -j3
199 targets, links clean; the only matches for "error" are the filename app/test_error_paths.cpp
```

## Tests

```
$ ctest --test-dir build/linux-core
100% tests passed, 0 tests failed out of 11
Total Test time (real) =   0.74 sec

$ QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --timeout 120
100% tests passed, 0 tests failed out of 23
Total Test time (real) =   5.55 sec
```

Both lanes were also run green inside the unit branch before the merge (11/11 and 23/23).

## New checks and whether they fail without the change

`core/tests/test_scan.cpp`

| Check | Proves | Fails without the fix |
|---|---|---|
| one series UID under two study UIDs is two series, not one | composite key | yes — unpatched tree reports 1 |
| the shared series UID is reported as one conflict split | diagnostic counter | yes (counter did not exist) |
| two patient IDs under one series UID is two series | identity conflict split | yes — unpatched tree reports 1 |
| one split is reported, not one per file | counter granularity | yes |
| both objects without a series UID are counted | quarantine counter | yes — objects were dropped |
| the orphans are reachable, not dropped | quarantine group | yes — unpatched tree reports 1 series |
| a rescan reports the same series, order, and files | deterministic grouping | no (guards a regression I nearly shipped) |

`core/tests/test_index_store.cpp`

| Check | Proves | Fails without the fix |
|---|---|---|
| both studies survive: the key is (study, series) | composite primary key | yes — v4 schema keyed on the UID alone |
| the quarantine flag survives the cache | quarantine round-trip | yes (column did not exist) |
| a version 4 cache is a miss, so the folder is rescanned | schema bump invalidates | yes |
| the aged cache is rebuilt from a fresh scan | cache usable after upgrade | yes — failed before `ensureSchema` was reordered |

Revert-check run on the unpatched tree (`git show work/release1:core/src/series_index.cpp`), same
fixtures, old binary:

```
two-studies: series=1 (expected 2 with the fix)
two-patients: series=1 (expected 2 with the fix)
no-series-uid: series=1 (expected 2 with the fix: named + quarantine)
```

## Two defects the tests found in the process

1. `IndexStore::ensureSchema()` created the series tables and then dropped them when the stored
   version disagreed, leaving no tables at all. The first save after any schema bump therefore failed
   and the cache stayed empty until a later open rebuilt everything. Fixed by deciding the version
   before creating tables.
2. The corpus contains two JPEG-LS objects with no `SeriesInstanceUID`. They were invisible before
   this unit (the scan reported 5 series); quarantined, they became a sixth group that sorted first
   and pushed real studies down. The quarantine group now sorts last.

## Not verified here

- Windows and macOS builds. Windows CI is the only Windows signal and needs a push to run.
- Anything about rendering or DPI: this unit is header parsing and SQLite.
- Behaviour on a real folder of several hundred studies; the fixtures are synthetic and small.
