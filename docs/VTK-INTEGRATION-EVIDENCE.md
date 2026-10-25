# VTK migration bundle: integration evidence

Bundle: `Clivus VTK bundle.zip` (VTK migration phases 1-4 plus the overlay-glyph fix),
overlaid onto `origin/main` at `997298b` — the exact base the bundle names, so nothing
was merged or rebased.

Branch: `work/vtk-migration`. VTK stays optional (`CLIVUS_BUILD_VTK_CANVAS`, default OFF);
the legacy canvas is still what the app draws with.

Three defects in the bundle had to be fixed before any of it compiled. Everything below
is output from this host (Raspberry Pi 5, Debian 13, aarch64, 4 cores, Qt 6.8.2,
DCMTK 3.6.9, VTK 9.3.0 from Debian `libvtk9-dev` + `libvtk9-qt-dev`).

## What the bundle could not have known: VTK 9.3 API drift

| Check | Bundle as delivered | Fixed to | Evidence |
|---|---|---|---|
| Configure | `project(clivus ... LANGUAGES CXX)` only; Debian's VTK links `VTK::mpi` → `MPI::MPI_C`, which cannot exist without a C compiler | `enable_language(C)` inside the `CLIVUS_BUILD_VTK_CANVAS` block (non-VTK builds untouched) | `CMake Error ... The link interface of target "VTK::mpi" contains: MPI::MPI_C but the target was not found ... An ALIAS target is missing` at `VTK-targets.cmake:563` |
| Configure | `COMPONENTS` omitted the two modules that own `vtkImageSlice`/`vtkImageSliceMapper` and `vtkImageViewer2` | added `RenderingImage InteractionImage` | link failure `undefined reference to 'vtkImageViewer2::New()'`; `nm -DC libvtkInteractionImage-9.3.so.1` is the library that exports it |
| Compile | `image->SetDirectionMatrix(const double[3][3])`, `image->GetDirectionMatrix(double[3][3])` | explicit `vtkMatrix3x3` (bridge) and a `copyDirection()` helper (handover test) | `error: no matching function for call to 'vtkImageData::SetDirectionMatrix(const double [3][3])'` — 9.3 offers `vtkMatrix3x3*`, `const double[9]`, or nine doubles |

The direction matrix is written element-by-element on purpose: a transposed matrix
mirrors the patient, and the flat-array overload makes that mistake easy.

## Default build (VTK off) — clean

```
cmake --preset linux -DCLIVUS_BUILD_VTK_CANVAS=OFF     # configure rc=0
cmake --build --preset linux -j4                        # [285/285], 0 errors, 0 FAILED
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --timeout 120
```

```
20/31 Test #20: overlay_widget ...................   Passed    0.04 sec
100% tests passed, 0 tests failed out of 31
Total Test time (real) =   9.13 sec
```

`overlay_widget` is new with the bundle and passes: the transparent child exists, covers
the canvas, takes no mouse input, and its own `grab()` renders the corner text through
the raster engine. The `cine` test that has flaked on this Pi passed here.

## The VTK gates — run for the first time

The bundle was written on a machine with no Qt and no VTK, so neither gate had ever
executed. They are Qt-free (CPU imaging filters), so they build and run here against
Debian's VTK 9.3 even though the QVTK widgets cannot (see below).

```
vtk_handover: 0 failure(s)                                                    exit=0
parity grey narrow:          max diff 1, pixels off by >1: 0/3072
parity grey wide:            max diff 1, pixels off by >1: 0/3072
parity grey narrow inverted: max diff 1, pixels off by >1: 0/3072
parity grey wide inverted:   max diff 1, pixels off by >1: 0/3072
colour bytes match legacy render() exactly  (checked, no failure printed)
vtk_parity: 0 failure(s)                                                      exit=0
```

The bridge's DICOM LUT reproduces the legacy `ImageFrame::render()` bytes on real VTK,
including MONOCHROME1 inversion and colour.

## Does the parity gate actually have teeth?

Each mutation was applied to `vtk_volume_bridge.cpp`, rebuilt, run, then reverted.

| Mutation | Result | Verdict |
|---|---|---|
| Inversion ignored (`const double v = gray;`) | `max diff 255, pixels off by >1: 3069/3072`, `vtk_parity: 4 failure(s)`, exit 1 | gate catches gross mapping errors |
| DICOM range `width - 1` replaced by `width` | `max diff 1, 0/3072`, **passes** | **gate is blind to this error** |

The second one matters: `width` vs `width - 1` is the exact difference
`docs/VTK-MIGRATION.md` cites to justify using its own LUT instead of
`vtkImageMapToWindowLevelColors` or `vtkImageViewer2`'s mapping. Shifting the range by
half a grey level moves no pixel past the ±2 tolerance, so the gate cannot defend the
decision it was written for. It needs a direct LUT check (scalar at the documented
`C - 0.5 - (W - 1)/2` maps to 0, at the upper bound maps to 255) rather than a
whole-image tolerance, if that claim is to be tested at all.

## NOT verified here

- **The QVTK widgets do not compile on this host.** Debian's VTK 9.3 reports
  `VTK_QT_VERSION 5`; Clivus is Qt 6. Configure dies with
  `The INTERFACE_QT_MAJOR_VERSION property of "Qt5::Core" does not ...`. So
  `vtk_canvas_widget`, `vtk_canvas_widget_events`, `vtk_plane_widget`,
  `vtk_mpr_dialog`, `vtk_volume_widget` and `vtk_canvas_spike` are reviewed but
  never compiled. They need a VTK built against Qt 6 (vcpkg on Windows, Homebrew on
  macOS) — "VTK 9.1 with Qt support" is not sufficient as a requirement; it has to say
  Qt 6.
- No visual or interaction testing: overlay compositing over a live GL surface, gesture
  handling, crosshair linking, MIP/MinIP slabs, volume-rendering opacity response.
- The overlay fix's original complaint (sheared glyphs on one Windows backend) cannot be
  reproduced here; the test proves the text goes through the raster engine, not that the
  reporter's backend is fixed.
- Numbers are from one aarch64 host with software GL. No Windows, no macOS, no DPI
  scaling, no GPU.
- `width`/`width - 1` blindness above: the MPR plane's own window/level mapping
  (`vtkImageViewer2`) is therefore untested against DICOM, and the dialog's "under a
  grey level" claim is unverified.
