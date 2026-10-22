# Windows verification runbook

The only open question the spike cannot answer on macOS: **does the canvas get a real GPU driver and
hold 60 fps on Windows?** Two sub-questions come with it — does the tree compile with MSVC, and does
VTK 9 build static there. This file is the order to answer them in, with pass criteria.

Everything below takes ~90 minutes on a fresh machine, most of it waiting for vcpkg.

## 0. Zero-cost route: run the CI-built folder on a borrowed machine

Nothing to buy, and no toolchain needed on the Windows machine.

1. Push this repo to GitHub (public = free minutes).
2. Actions → **windows-app** → *Run workflow*. It builds with MSVC, runs `ctest`, and uploads a
   `horoswin-windows-x64` artifact: the exe, Qt DLLs, the DCMTK DLLs, and the test corpus.
3. On any Windows machine — a friend's laptop, a lab PC, work — download the artifact, unzip, then:

   ```powershell
   .\horoswin.exe .\corpus --bench 150
   ```

That is the whole test: two minutes, no installs. Only two lines matter: the `GL …` renderer line and the
`bench …` line. An ordinary laptop iGPU is a *better* answer than a virtual machine's virtual GPU,
so a borrowed one is not a compromise.

The runner has no GPU, so its own bench run (software GL) proves only that the binary loads and the
canvas can create a context — it never answers the fps question.

## 1. Pick the box

| Option | Cost | Answers |
|---|---|---|
| **Borrowed Windows laptop + the CI artifact** (§0) | **free** | the real answer, on real silicon |
| **Parallels Desktop + Windows 11 ARM** on this Mac | ~$100/yr, free Windows ARM ISO via Microsoft | compile, run, GL renderer name, fps *indicative* |
| **VMware Fusion 13** (free for personal use) + Windows 11 ARM | free | same as above, weaker 3D |
| Used x86-64 mini PC with a real GPU (e.g. GTX/RTX or Iris Xe) | ~$200–400 | the actual answer: 60 fps on real silicon |
| GitHub Actions `windows-latest` | free | compile only — no GL, so **not** an answer for fps |

UTM/QEMU works for compiling but its virtual GPU has no usable OpenGL: a bench there measures
software rasterisation and is worse than no measurement.

Two traps that waste an afternoon:

- **Don't benchmark over RDP.** An RDP session gets a software GL implementation (GDI Generic), so
  every number is meaningless. Run the bench on the console session of the VM or the PC.
- **Qt 6 on Windows needs OpenGL 2.0+ from the driver.** If it doesn't get one it falls back to
  `opengl32sw.dll` (llvmpipe) silently. The app prints the renderer at startup for exactly this reason —
  see §4.

## 2. Toolchain

In the guest, install:

1. **Visual Studio 2022 Build Tools** with "Desktop development with C++" (MSVC v143 + Windows 11 SDK).
2. **Git**, **CMake ≥ 3.25**, **Ninja** (VS ships CMake; Ninja via `winget install Ninja-build.Ninja`).
3. **vcpkg**, then let the manifest do the rest:

   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\vcpkg
   ```

4. **Qt 6.x for MSVC 2022 64-bit** — `aqtinstall` is the least clicking:

   ```powershell
   pip install aqtinstall
   aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 --outputdir C:\Qt
   ```

   `vcpkg install qtbase` also works but compiles Qt from source (>1 h).

## 3. Build and test

```powershell
cd <repo>
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.1\msvc2022_64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

First configure compiles DCMTK + OpenJPEG from source through vcpkg (~20–30 min, cached afterwards).

**Pass criteria for this step**

- [ ] all three tests green: `image_frame`, `decode_matrix`, `index_store`
- [ ] `decode_matrix` reports `decoders in this build: RLE, JPEG, JPEG-LS, JPEG 2000`
- [ ] `MR_small_jp2klossless.dcm` row says **`OK … identical to MR_small.dcm`** (this is the J2K
      decoder that Homebrew's DCMTK does not have — it is the reason `vcpkg.json` asks for `openjpeg`)
- [ ] no row says `UNSUPPORTED`

## 4. Measure

Get a real series first — the runbook assumes `testdata/corpus` (committed) and, optionally, a TCIA
series in `testdata\real\` (50 slices, 512², from `4D-Lung`):

```powershell
build\Release\app\horoswin.exe testdata\corpus --bench 150
build\Release\app\horoswin.exe testdata\real --bench 150
```

The first thing to read is the line the canvas prints at startup:

```
GL 2.1 Metal - 91.7 | Apple | Apple A18 Pro        <- macOS reference, real driver
GL 1.1.0 | Microsoft Corporation | GDI Generic     <- fallback, discard the numbers
```

**Pass criteria**

- [ ] renderer names a real GPU (NVIDIA / AMD / Intel / Parallels / VMware SVGA), not GDI Generic,
      not llvmpipe, not `opengl32sw`
- [ ] `fps median 60` at 512² and 1024² (60 = vsync; that is the target, not the ceiling)
- [ ] `window median` < 4 ms and `paint median` < 4 ms at 512² — macOS reference is 0.4 ms, so anything
      under 4 ms still leaves 12 ms of the frame for the toolkit
- [ ] the same run twice: second start reports `scan: 1.2 ms`-ish from the on-disk index

If fps is 60 but the paint median is > 8 ms, the GPU is fine and the driver is doing something odd
(sync-to-vblank stalls, or MSAA forced on in the driver panel) — check driver settings before blaming
the canvas.

## 5. Then VTK (v1 scope, separate step)

VTK 9 is the remaining unknown for MPR/3D, and it changes the canvas:

```powershell
vcpkg install vtk[core,opengl,renderingcontext]:x64-windows-static
```

Static is deliberate: it keeps the installer a single tree without a DLL zoo. Then replace the canvas
with `QVTKOpenGLNativeWidget` and re-run §4 — the fps question is unchanged, the widgets are new. Do
not start this before §4 passes, or two unknowns get debugged at once.

## What to send back

The full console output of §3 and §4 — the decoder line, the GL line, the bench lines. That is enough
to decide whether the canvas is done or needs shaders instead of fixed-function GL.
