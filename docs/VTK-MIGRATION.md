# VTK migration: from the legacy GL canvas to VTK 9

Goal, stated plainly: a better Horos for Windows researchers. VTK does not
add MPR as a feature — CPU MPR already ships — it moves reslicing onto
the GPU, makes oblique planes possible (today's planes are axis-aligned
only), gives linked crosshairs, and unlocks volume rendering. The
migration is phased so that no phase ships a picture the previous phase
cannot prove.

## Decision: VTK stays optional until parity is proven

VTK is not in `vcpkg.json` and does not become a required dependency in
Phases 1–4. Everything builds behind `CLIVUS_BUILD_VTK_CANVAS` (default
OFF) against a system VTK 9.1 with Qt support; a box without VTK builds
exactly as before, and the legacy canvas stays the default. Making VTK a
vcpkg dependency (and therefore required) is a post-Phase-2 decision, to
be taken only once the VTK canvas has proven pixel parity — VTK is one
of the biggest ports in vcpkg, and Windows CI time and artifact size
will feel it.

## Phase 1 — spike (built)

- `app/vtk_volume_bridge.{h,cpp}`: `Volume` -> `vtkImageData` handover
  (dimensions, spacing, origin, orientation, float modality voxels copied).
  Qt-free and rendering-free, so it is testable offscreen. Also
  `vtkImageDataFromFrame()` (one frame -> image, colour as unsigned-char
  RGB) and `dicomLookupTable()` (see Phase 2).
- `app/vtk_canvas_spike.cpp`: demo executable — the middle slice of a
  real volume on the Phase 2 canvas, with wheel stepping through the
  stack.
- `app/test_vtk_handover.cpp`: the handover, checked without rendering.
- Build: `cmake -DCLIVUS_BUILD_VTK_CANVAS=ON`, then
  `ctest -R "vtk_handover|vtk_parity"` and
  `vtk_canvas_spike <dicom-folder>`.

## Phase 2 — 2D parity (widget built, cutover not yet done)

- `app/vtk_canvas_widget.{h,cpp}` + `vtk_canvas_widget_events.cpp`: a
  `QVTKOpenGLNativeWidget` canvas that takes frames, not volumes, with
  the same API surface the main window uses on the legacy canvas:
  window/level (Horos drag conventions), pan, zoom about the pointer,
  wheel/keys/middle-drag slice stepping, double-click/R reset, fit view,
  measure and ROI tools with the same cursors and drag semantics, pixel
  readout, corner overlay text, and the same signals (`windowChanged`,
  `viewChanged`, `scrollRequested`, `pixelInfoChanged`, `roisChanged`).
  Pan/zoom are the legacy canvas's own state driven into the VTK camera,
  so framing matches the legacy canvas exactly.
- Window/level is NOT `vtkImageMapToWindowLevelColors`. That filter maps
  [level - window/2, level + window/2] with slope 255/window; DICOM's
  linear VOI maps a range of `width - 1` and MONOCHROME1 inverts it. The
  widget uses the shared `dicomLookupTable()` through
  `vtkImageMapToColors` instead.
- Overlays are NOT VTK actors and never `vtkTextActor` — after the 0.4.1
  garbled-glyph report, text goes through the same raster `OverlayWidget`
  the legacy canvas uses. The widget class is now shared: it takes paint
  and validity callbacks, and each canvas passes its own. The VTK
  widget's drawing code is a port of the legacy canvas's, kept in step
  with it; factoring the two ports into one shared implementation is
  follow-up work, not assumed done.
- VTK's own interactor is silenced (`vtkInteractorStyleUser`, and the Qt
  handlers never forward to the base class): one interaction path, no
  double-applied drag.
- VTK displays row 0 at the bottom; the legacy canvas draws row 0 at the
  top. The display image's rows are reversed at `setFrame()` so the
  picture matches — the stored frame is untouched.
- `app/test_vtk_parity.cpp` (`vtk_parity` in CTest): the cutover gate.
  Legacy `ImageFrame::render()` bytes vs the VTK pipeline (bridge image +
  DICOM LUT + `vtkImageMapToColors`), normal and inverted, narrow and
  wide windows, plus colour bytes exactly. Tolerance is honest, not zero:
  the LUT has 4096 entries, so no pixel may be off by more than 2 grey
  levels and fewer than 0.1% by more than 1. A systematic mapping error
  fails this by hundreds of levels, not by one.
- NOT done in Phase 2: the main-window canvas swap. The main window
  still uses the legacy canvas; the VTK canvas is compiled into VTK
  builds and demonstrated by the spike. The swap needs the dual-format
  fix (below) plus running `vtk_parity` green on the Windows builder,
  and it is deliberately not attempted in the same change as the widget.
- The format problem, still open: the app sets a GL 2.1 compatibility
  format globally (`main.cpp`); VTK 9 needs core-profile 3.2+. The spike
  sidesteps this by being a separate executable with its own format.
  The integration must give the VTK widget a core-profile format
  explicitly rather than inheriting the app's global default.

## Phase 3 — MPR on VTK (built, as a dialog)

- `app/vtk_plane_widget.{h,cpp}`: one orthogonal plane on
  `vtkImageViewer2` (XY/XZ/YZ), which already knows how to orient a
  slice correctly. Gestures in Qt (left-drag moves the shared crosshair
  point, right-drag pans via picking, wheel steps slices, ctrl+wheel
  zooms); the crosshair is drawn by the raster overlay, never a VTK
  actor. Window/level goes through the viewer's own mapping here, which
  is close to DICOM but not identical (its range is `width`, DICOM's is
  `width - 1`); for typical widths the difference is under a grey level,
  and the dialog says so. The 2D canvas, which carries the parity gate,
  uses the parity-tested DICOM LUT instead.
- `app/vtk_mpr_dialog.{h,cpp}`: three planes plus volume rendering in a
  2x2 dialog for one series, opened from the View menu ("VTK view
  (research/experimental)") under the same geometry-eligibility rule as
  MPR. The dialog owns one shared point: clicking a plane sets all three
  indices from the picked world point; moving one plane's slider moves
  only that plane. Slabs are CPU `resliceVolume()` frames (the same code
  the legacy MPR uses) shown in the plane widgets, centred on the plane's
  current index; the interpolation control applies to slab computation
  only, and is disabled while slabs are off.
- CPU `resliceVolume()` stays as the test reference for any future
  VTK-resliced plane; oblique planes and a VTK-native reslice cursor are
  the next step, not this one.

## Phase 4 — volume rendering (built, in the same dialog)

- `app/vtk_volume_widget.{h,cpp}`: `vtkSmartVolumeMapper` (the GPU
  ray-cast mapper when one is available — the pipeline the headless
  `vtk_probe` already proved), grey colour ramp over the volume range,
  opacity ramp that opens across the dialog's window, so moving the
  window sculpts the volume. Labelled experimental in the dialog; the
  probe's interactive-cost numbers are the CI gate before this is ever
  more than that.

## What does not change

- DCMTK decoding and the Qt-free core: only presentation moves.
- The fidelity contract: every new path gets a comparison test against
  the old path before it becomes default.
- The research-use limitation, stated in the app and the docs.

## Verification status
Phase 1 and the Phase 2 parity gate have now been run against real VTK (9.3 from Debian)
on the Raspberry Pi: `vtk_handover` and `vtk_parity` both pass, and the default
(VTK-off) build and its 31 tests are green. Everything below that touches a
`QVTKOpenGLNativeWidget` is still uncompiled — Debian's VTK is built against Qt 5 and this
app is Qt 6, so the widgets need a VTK built with **Qt 6** (vcpkg on Windows, Homebrew on
macOS). Full evidence, including which checks fail without the change and what remains
unverified, is in `docs/VTK-INTEGRATION-EVIDENCE.md`.

- `cmake -DCLIVUS_BUILD_VTK_CANVAS=ON` configures against system VTK
  9.1; the API names to watch are `vtkImageViewer2`'s module,
  `vtkImageSliceMapper`'s default orientation for a 2D image,
  `vtkLookupTable::SetTableValue` without `Build()`, and
  `QVTKOpenGLNativeWidget::interactor()`.
- `ctest -R "vtk_handover|vtk_parity"` green, offscreen.
- `vtk_canvas_spike <dicom-folder>` shows the middle slice, drag
  windows, wheel steps, double-click resets.
- In a VTK build, View -> VTK view opens the dialog: three planes,
  linked crosshair, MIP/MinIP slabs, volume rendering that responds to
  the window fields.
- Gestures on the VTK canvas do not double-apply (the interactor really
  is silent) and the planes are not mirrored or vertically flipped
  against the legacy MPR view.
