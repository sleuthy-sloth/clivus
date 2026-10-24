# Unit 5 evidence — speculative decode isolation

Unit 5 as planned has two halves: stop a requested frame waiting behind speculation, and measure
slice-step latency (p50/p95/p99). **Half one is done. Half two is not, and on this host it cannot be
done** — which is the main thing this file has to say.

```
UNIT 5 — prefetch off the foreground pool
base:    origin/main@d9dfa85 (via work/release1@f85ad43)   branch: work/release1
files:   app/main_window.h  | 10 ++++++++--
         app/main_window.cpp | 13 +++++++++----
```

## What changed

`pumpSlice()` and the neighbour prefetch both ran on `m_foreground`, a one-thread pool
(`QtConcurrent::run(&m_foreground, ...)` at `main_window.cpp:1200` and `:1275`). `QtConcurrent` has no
cancellation: a task handed to a pool runs, and a queued one waits. So on a direction reversal the
frame the user asked for queued behind a decode of the frame they had just scrolled away from.

The speculative decode now runs on its own single-thread pool, `m_prefetch`:

- a requested frame never queues behind speculation;
- "one decode at a time" still holds *within* each pool;
- epoch checks are unchanged, so a prefetch landing after a series switch is still dropped.

Cost, stated rather than hidden: a prefetch and a requested decode can now run simultaneously, so a
direction reversal briefly uses a second core instead of serialising.

The review's own prescription — "cancel/discard stale prefetch on direction changes" — is not
implementable as written. `QFuture::cancel()` on a `QtConcurrent::run` task does not stop it; this
change removes the *consequence* of not being able to cancel, rather than pretending to cancel.

## Verification — and what it does not prove

```
$ cmake --build build/linux -j3         → 0 errors
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --timeout 120
100% tests passed, 0 tests failed out of 26
Total Test time (real) =   5.55 sec
$ ctest --test-dir build/linux-core
100% tests passed, 0 tests failed out of 11
Total Test time (real) =   0.72 sec
```

**There is no automated test for this change, and I am not going to pretend there is one.** The failure
mode is a timing one: it needs a slow decode and a reversal mid-flight, and there is no seam to inject
a slow `loadFrame` without inventing one just for the test. A test asserting "prefetch used a
different pool pointer" would restate the diff and protect nothing.

The smoke run is also useless here: the app's `--bench` mode reports nothing on this host because the
offscreen platform cannot create a GL context, so no frames are painted.

```
$ QT_QPA_PLATFORM=offscreen ./build/linux/app/clivus testdata/corpus --bench 12
QOpenGLWidget: Failed to create context
(no bench line: nothing was painted)
```

## What would actually prove it

A slice-step latency benchmark, which is the other half of the unit and does not exist yet:

1. Drive `goToSlice()` forward and backward through a stack rather than repainting one frame (the
   current bench changes window/level, which does not touch the decode pipeline at all).
2. Record input → target chosen → decode start → decode end → present → paint, per step, and report
   p50/p95/p99 for forward runs, backward runs and reversals separately.
3. Run it on a machine with a working GL context, before and after this change, and compare reversal
   p95. That number is the only thing that can honestly call this an improvement.

Until then the honest statement is: the mechanism that made reversals slow is gone by construction,
and the measurement that would confirm the effect has not been taken.
