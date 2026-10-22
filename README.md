# horoswin — spike

A **new** Windows-native DICOM viewer, workflow-compatible with Horos/OsiriX.
Not a fork: no Horos source is copied here (see `../horos-port/HOROS-WINDOWS-PORT-FEASIBILITY.md`
for the reasoning and the measured evidence). Working name only — "Horos" and "OsiriX" are
trademarks with no grant in the upstream licence.

## What this spike is for

One question: **does a Qt6 + OpenGL canvas hold 60 fps on real series while windowing?**
Everything else here exists only to make that measurable.

- `core/` — Qt-free, unit-tested: DICOM series scan (DCMTK), modality-value frame, DICOM linear
  VOI LUT window/level.
- `app/` — Qt6 Widgets shell: series list with thumbnails, GL canvas, drag-to-window, pan, zoom,
  wheel-to-scroll, live frame timings in the status bar.

## Build

macOS (Homebrew Qt6 + DCMTK):

```bash
brew install qt dcmtk
cmake --preset mac && cmake --build --preset mac
build/mac/core/hw_test_image_frame                 # pixel-math checks
build/mac/core/hw_test_decode testdata/corpus      # codec matrix
build/mac/core/hw_measure testdata/corpus          # headless decode + window cost
build/mac/app/horoswin testdata/corpus             # GUI
build/mac/app/horoswin testdata/synthetic512 --bench 150   # fps at 512²
```

`testdata/make_synthetic.py` writes an 8-slice signed 12-bit CT series with slope/intercept and
PixelPaddingValue, so the loader is testable without patient data; `testdata/corpus/` also holds
two small pydicom sample objects (CT_small, MR_small) for a real-world sanity check.

Windows (MSVC + vcpkg; needs `VCPKG_ROOT` set):

```powershell
cmake --preset win
cmake --build --preset win
ctest --preset win
```

CI builds and tests **core only** on `windows-latest` (`.github/workflows/windows.yml`);
`vcpkg install dcmtk` from source is slow the first time and Qt is deliberately not installed there.

## Codecs — what actually decodes

`hw_test_decode testdata/corpus` is the matrix. Two findings it exists for:

1. **DCMTK registers no decoder by default.** RLE, JPEG, JPEG-LS and JPEG 2000 each need their own
   `…Registration::registerCodecs()` call; without it every compressed object fails with
   `can't change to unencapsulated representation for pixel data`. `core/src/codecs.cpp` does it once,
   guarded by `__has_include` on the codec headers so a DCMTK built without a codec still compiles.
2. **Whether JPEG 2000 decodes is a property of the DCMTK build, not of DCMTK.** Homebrew's does not
   include it (`decoders: RLE, JPEG, JPEG-LS`); vcpkg's does with the `openjpeg` feature, which is why
   `vcpkg.json` requests it — on Windows CI the matrix must decode `MR_small_jp2klossless.dcm` and fail
   if it doesn't.

| Transfer syntax | Test object | This build (brew DCMTK 3.7.1) | Windows CI (vcpkg + openjpeg) |
|---|---|---|---|
| Explicit VR LE | CT_small, MR_small | ok | ok |
| RLE Lossless | MR_small_RLE (16-bit), rtdose_rle (32-bit, 15 frames) | ok, pixel-identical to MR_small | ok |
| JPEG-LS Lossless / Near-lossless | MR_small_jpeg_ls_lossless, JPEGLSNearLossless_08/16 | ok, lossless variant pixel-identical | ok |
| JPEG 2000 Lossless / Lossy | MR_small_jp2klossless, JPEG2000, 693_J2KI | **not in this build** | asserted identical to MR_small |

The three lossless variants of `MR_small` must decode to **exactly** the uncompressed pixels, so a
DCMTK upgrade that silently changes decompressed values fails the test instead of quietly changing
what a radiologist sees. Lossy and near-lossless objects are smoke-tested for dimensions only.

Two loader behaviours the matrix forced:

- Encapsulated pixel data is decompressed with `DcmItem::chooseRepresentation(EXS_LittleEndianExplicit)`
  before `DicomImage` sees it. `DicomImage`'s own encapsulated path rejects valid objects (32-bit RLE
  RT Dose), while the codec path handles them — one code path for every file, compressed or not.
- 32-bit multi-frame objects hand back the whole frame stack in one buffer, so the loader clamps to
  `width*height` (frame 0) rather than assuming one frame.

RT Dose also carries `DoseGridScaling` instead of `RescaleSlope`, which the loader does not apply yet
(hence raw ranges like [795000, 1254000] for `rtdose_rle.dcm`) — a ticket, not a bug in decoding.

## Measured (Apple M-series, macOS, Qt 6.12 + DCMTK 3.7.1, release build)

`horoswin <folder> --bench N` repaints the canvas N times with the window/level nudged every
frame, i.e. it pays the full drag cost (re-window 8-bit + texture upload + draw). fps is
vsync-capped at 60 here; the `window`/`paint` medians are the headroom signal:

| Image | window (CPU, ms) | paint (CPU, ms) | fps | decode (ms/slice) |
|---|---|---|---|---|
| 512×512 | 0.34 | 0.37 | 60 (capped) | 0.7 |
| 1024×1024 | 1.16 | 1.18 | 60 (capped) | 2.8 |

Windowing alone (`hw_measure`, no GL): 0.20 ms at 512², 0.80 ms at 1024². Folder scan,
header-only: ~5 ms for 10 objects. So the CPU side of a drag is ~2 orders of magnitude
under the 16.7 ms budget — the 60 fps question is now about GL/VTK on Windows, not about
our pixel path.

## Deliberately not here yet

- SQLite index / persistence (P0; scan is in-memory, synchronous on the UI thread)
- Colour images, multi-frame objects (frame 0 only), presentation LUTs, MONOCHROME1
- JPEG 2000 **on macOS**: brew's DCMTK has no J2K decoder, so J2K is only exercised by Windows CI
  (vcpkg `dcmtk[openjpeg]`) until this tree vendors its own DCMTK build
- VTK / MPR / 3D (decided for v1, starts after the spike)
- Measurement tools, PACS networking, plugin ABI
- Any Grøk/AGPL component — the shipped Horos 4.0.1 statically contains it; this tree does not

## Next tickets after the spike

1. ~~Real corpus + codec matrix~~ (done: `core/tests/test_decode.cpp`; J2K asserted on Windows CI)
2. SQLite series index with restart persistence
3. Window/level correctness vs Horos on the same objects (WC/WW parity test, pixel diff)
4. VTK 9 static build + `QVTKOpenGLNativeWidget` swap for the canvas
