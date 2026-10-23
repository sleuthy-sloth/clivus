# Clivus

[![Windows core tests](https://github.com/sleuthy-sloth/clivus/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/sleuthy-sloth/clivus/actions/workflows/windows.yml)
![Windows GUI build](https://github.com/sleuthy-sloth/clivus/actions/workflows/windows-app.yml/badge.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![Qt 6](https://img.shields.io/badge/Qt-6.8%2B-41CD52?logo=qt&logoColor=white)
![Licence](https://img.shields.io/badge/licence-GPL--3.0--or--later-blue)

A cross-platform DICOM viewer in the Horos/OsiriX tradition, built Windows-first.
C++20 + Qt 6 + DCMTK + OpenGL, with one working rule: **nothing stays in this tree
that can't be measured.**

> **Research tool, not a medical device.** No regulatory clearance, no clinical
> validation. Don't use it for diagnosis or any decision affecting patient care.

It follows the Horos/OsiriX workflow but shares no code with them — this is a new
codebase, not a port. "Horos" and "OsiriX" are trademarks of their respective
owners.

## Why the repo looks like a spike

DICOM viewers usually die in two places: pixels that quietly lie, and a UI that
can't keep up with a real series. So Clivus started as a single question: **does a
Qt6 + OpenGL canvas hold 60 fps on real series while windowing?** Everything here
exists to answer that question, and the answer is *yes, with room to spare* — the
numbers are below.

- **`core/`** — Qt-free, unit-tested: DICOM series scan (DCMTK), modality-value
  frames, linear VOI LUT window/level, whole-volume geometry. Builds without any
  GUI toolkit.
- **`app/`** — Qt6 Widgets shell: series list with thumbnails, GL canvas,
  drag-to-window, pan, zoom, wheel-to-scroll, live frame timings in the status
  bar.

## Every supported transfer syntax must *prove* it decodes

`clivus_test_decode testdata/corpus` is the codec matrix. Two findings shaped it:

1. **DCMTK registers no decoder by default.** RLE, JPEG, JPEG-LS and JPEG 2000
   each need their own `…Registration::registerCodecs()` call; without one, every
   compressed file fails with `can't change to unencapsulated representation for
   pixel data`. `core/src/codecs.cpp` does all of it once, guarded by
   `__has_include`, so a DCMTK built without a codec still compiles.
2. **Whether JPEG 2000 decodes belongs to the DCMTK build, not to DCMTK.**
   Homebrew's build ships no J2K decoder; vcpkg's does, via the `openjpeg`
   feature. That's why `vcpkg.json` asks for `openjpeg` and why Windows CI
   *asserts* the J2K rows decode pixel-identically rather than merely trying.

| Transfer syntax | Test objects | brew DCMTK 3.7.1 | Windows CI (vcpkg + openjpeg) |
|---|---|---|---|
| Explicit VR LE | CT_small, MR_small | ok | ok |
| RLE Lossless | MR_small_RLE (16-bit), rtdose_rle (32-bit, 15 frames) | ok, pixel-identical to MR_small | ok |
| JPEG-LS Lossless / Near-lossless | MR_small_jpeg_ls_lossless, JPEGLSNearLossless_08/16 | ok, lossless variant pixel-identical | ok |
| JPEG 2000 Lossless / Lossy | MR_small_jp2klossless, JPEG2000, MR2_J2KI | not in this build | asserted identical to MR_small |

The three lossless variants of `MR_small` must decode to **exactly** the
uncompressed pixel values, so a DCMTK upgrade that silently changes decompressed
output fails the suite instead of a radiologist. Lossy and near-lossless objects
are smoke-tested for dimensions only.

Two loader behaviours this matrix forced:

- Encapsulated pixel data is decompressed through the registered-codec path
  (`chooseRepresentation(EXS_LittleEndianExplicit)`) before `DicomImage` sees it,
  because `DicomImage`'s own encapsulated route rejects valid objects such as
  32-bit RLE RT Dose. One code path for every file.
- 32-bit multi-frame objects hand back the whole frame stack in one buffer, so the
  loader clamps to `width*height` (frame 0) rather than assuming a single frame.

## Measured, not promised

`clivus <folder> --bench N` repaints the canvas N times with the window/level
nudged every frame — full drag cost: re-window, texture upload, draw. Apple
M-series, Qt 6.12 + DCMTK 3.7.1, release build:

| Image | window (CPU, ms) | paint (CPU, ms) | fps | decode (ms/slice) |
|---|---|---|---|---|
| 512×512 | 0.34 | 0.37 | 60 (vsync-capped) | 0.7 |
| 1024×1024 | 1.16 | 1.18 | 60 (vsync-capped) | 2.8 |

Windowing alone, no GL (`clivus_measure`): 0.20 ms at 512², 0.80 ms at 1024². The
CPU side of a drag sits two orders of magnitude under the 16.7 ms frame budget —
the remaining question for Windows is the GPU path, not this pixel path.

## The series index: restart persistence with nothing to invalidate

Opening a folder writes `<folder>/.clivus-index.sqlite` next to the data, keyed on
a fingerprint of the folder — file count, newest mtime, total bytes. Unchanged
folder, cached results in ~1 ms. Changed folder, automatic miss and full rescan.
There is no invalidation logic, because there is nothing to invalidate:

| 50-slice real CT series | scan |
|---|---|
| first start, no index | 29.7 ms |
| every start after | 1.2–1.7 ms |

## Volume stacks: geometry first, filenames never

`loadVolume()` orders a series **by position along the slice normal** — not by
`InstanceNumber` and not by filename. Real series arrive shuffled, and MPR/3D
produce wrong images if you trust either. Spacing uses `PixelSpacing` plus the
median gap between slice positions. Its test is adversarial on purpose: a
committed 4-slice series whose pixel values name their own index and whose
filenames run backwards, so trusting files *or* `InstanceNumber` fails it.

On a real TCIA `4D-Lung` series (50 × 512²): whole stack in **58–100 ms**. VTK
rendering is opt-in (`cmake --preset mac-vtk`, `CLIVUS_BUILD_VTK_PROBE=ON`) via
`app/vtk_probe.cpp`, deliberately Qt-free; it renders that stack at 8 ms/frame on
the GPU after a 163 ms first frame.

A test-design note worth keeping: the tests are `assert`-based, and `Release`
builds define `NDEBUG`, which erases every check while still printing "all checks
passed". `core/CMakeLists.txt` strips `NDEBUG` for test targets — without that,
the CI suite proved nothing.

## Try it

**Every platform source:**

```bash
# macOS (Homebrew Qt6 + DCMTK)
brew install qt dcmtk
cmake --preset mac && cmake --build --preset mac
build/mac/core/clivus_test_decode testdata/corpus        # codec matrix
build/mac/app/clivus testdata/corpus                     # GUI
build/mac/app/clivus testdata/synthetic512 --bench 150   # fps at 512²
```

```powershell
# Windows (MSVC + vcpkg; set VCPKG_ROOT first)
cmake --preset win
cmake --build --preset win
ctest --preset win
```

CI builds and tests **core only** on every commit; a manual workflow compiles the
full Qt GUI with MSVC. Neither can measure fps — that needs a real GPU. But you
don't need any of this: the [Windows GUI workflow](https://github.com/sleuthy-sloth/clivus/actions/workflows/windows-app.yml)
uploads a runnable **`clivus-windows-x64`** artifact (exe + Qt/DCMTK DLLs + test
corpus). Download it to any Windows machine and run:

```powershell
clivus.exe .\corpus --bench 150
```

`testdata/make_synthetic.py` generates an 8-slice signed 12-bit CT series with
slope/intercept and `PixelPaddingValue`, so the loader is testable without patient
data; `testdata/corpus/` holds ten small pydicom sample objects for real-world
sanity checks. The full runbook with pass criteria is
[`docs/windows.md`](docs/windows.md).

## Status

Shipped: explicit-VR LE, RLE, JPEG, JPEG-LS decoding plus JPEG 2000 where the
DCMTK build carries it; the SQLite series index; window/level, pan, zoom;
volume loading with geometry-aware ordering; the VTK probe.

Deliberately not here yet:

- Colour images, multi-frame objects (frame 0 only), presentation LUTs
- JPEG 2000 on macOS (brew's DCMTK lacks the decoder until this tree vendors its
  own DCMTK build; Windows CI covers it now)
- DoseGridScaling (RT Dose reads as raw ranges until it lands — a ticket, not a
  decode bug)
- VTK / MPR / 3D in the real viewer (decided for v1; the probe is the proof)
- Measurement tools, PACS networking, plugin ABI
- No AGPL code of any kind in this tree; the Horos-derived AGPL content shipped in
  Horos 4.0.1 stays outside

Remaining from the spike:

1. Window/level correctness vs Horos on the same objects (WC/WW parity, pixel diff)
2. VTK 9 + `QVTKOpenGLNativeWidget` swap for the canvas

## Licence

GPL-3.0-or-later — see [`LICENSE`](LICENSE). Qt 6 is used under its GPL-3.0
option, so binaries built from this repository are GPL-3.0-or-later as a whole.
DCMTK, SQLite, VTK and the JPEG-family codecs keep their permissive licences;
terms and required notices in [`THIRD-PARTY.md`](THIRD-PARTY.md).
