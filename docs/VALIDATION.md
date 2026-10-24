# Validation status

What has actually been run, on what, and what remains unverified. The rule this file exists to
enforce: a green workflow is not a tested application, and nothing here claims clinical validation.

## What is verified on every change

- **Core library + CLI tools** build on Linux (aarch64, CMake/Ninja, Qt 6.12, DCMTK 3.7.1) and macOS
  (GitHub runner, brew Qt + DCMTK) with no errors. The core also builds and tests against **Debian 13
  aarch64 with DCMTK 3.6.9**, which is what keeps the `DCMTK_VERSION` differences honest (see
  `docs/BASELINE-2026-10-24.md`).
- **27 test targets** run green locally (26 in the full build, plus the core-only parity gate):
  image_frame, decode matrix, colour/photometric, volume + MPR reslice, index_store (cache),
  manual_search, parity gate, scan, series_filter, settings_roundtrip, error_paths, measurements,
  mpr, help_search, series_summary, annotations, cine, export, dicomweb, server_dialog, browser_model,
  overlay_render, canvas_view.
- **The GUI builds** on both platforms as part of those test targets (the harnesses link
  `main_window.cpp`, `canvas_widget.cpp`, and the rest), and the packaged Windows build is produced
  by `.github/workflows/windows-app.yml` (Qt deployment + bundled DCMTK DLLs + assets + licence
  files). That workflow is **manual-dispatch only**, so a release tag does not build Windows by
  itself — say so in the release notes rather than implying otherwise.
- **Real-UI screenshots** are produced by the application itself (`--capture`) on a macOS runner that
  has a window server (`.github/workflows/screenshots.yml`). That run is also the only automated
  proof that the OpenGL canvas paints real pixels — a headless Linux host cannot create a GL context.
  Consequence worth stating: on a headless host the overlay drawing is tested by painting it into a
  `QImage` (`overlay_render`), which proves the drawing code and not the framebuffer.

## What is *not* verified here

| Area | Status | Why |
| --- | --- | --- |
| Windows GUI launch on real hardware | **Not run** | No Windows machine available to the project. CI builds and packages it; nobody has double-clicked the released exe. |
| Exe icon in Explorer / Start menu / Alt-Tab | **Not run on Windows** | Requires the packaged exe on a Windows desktop. The RC resource and `.ico` are in the build. |
| High-DPI scaling, multi-monitor, resizing while an image is shown | **Not run** | Requires a desktop session with those displays. |
| GPU-driver behaviour (real GPU vs software fallback) | **Partially** | The macOS runner exercises the canvas; the Linux box has no GL context at all. The Windows CI job runs `--bench` under software GL only to prove the binary starts. |
| Multi-hundred-slice clinical-speed studies | **Not run at scale** | The committed corpus is synthetic; no large dataset is available (and none may be committed). |
| Installer / shortcuts | **Out of scope** | The release ships an unzipped folder, no installer. |

## Exact remaining validation steps (for anyone with a Windows machine)

1. Download `clivus-v*-windows-x64.zip` from Releases and unzip to a fresh folder (not Program Files,
   to prove no write access is needed).
2. Launch `clivus.exe`; the window should show the welcome screen with the Clivus lockup.
3. Check the icon in Explorer (before launch), in the taskbar while running, and in Alt-Tab.
4. Open `corpus\` from the app (or drop the folder onto the window). Series list should populate with
   thumbnails; the RT Dose row shows 1 object / 15 frames.
5. Select the CT series: the image renders right-way-up; wheel scrolls slices; left-drag adjusts
   window/level; right-drag pans; `Ctrl`+wheel zooms; `R` or double-click resets.
6. Press `M`, drag a distance, Shift-click three points for an angle; Tools ▸ Clear measurements.
7. View ▸ MPR: three planes appear (the corpus CT is a single slice, so expect the eligibility gate —
   use a multi-slice study to see planes fill).
8. F1 opens the manual; type "millimetres" into its search — the Measuring chapter should match on
   body text, with an excerpt in the row tooltip.
9. View ▸ Clear index cache, reopen the folder — it rescans and works.
10. Move the unzipped folder elsewhere and relaunch — branding and assets must still load.
11. Close the app; confirm it exits cleanly and no `.clivus-index.sqlite` was created inside the
    DICOM folder.

Report results with the app version, the study layout (file count, single/multi series) and the
status-bar text on any failure. Windows-specific failures should include the exact step and whether
the canvas or the chrome was affected.

## Release reporting

Each release notes the version, the commit, the build environment, the tests that ran, whether the
GUI build ran, whether the packaged application was launched (and where), known limitations, and the
outstanding items above. "Not run" is recorded rather than implied to have passed.
