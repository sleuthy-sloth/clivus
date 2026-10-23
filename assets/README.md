# Brand assets

The Clivus identity ships in this folder. Nothing here needs regenerating per build; the app looks
for these files, in order:

1. `assets/…` next to the packaged executable
2. `../assets/…` (build-tree runs)
3. this folder in a source checkout (development runs, via `CLIVUS_SOURCE_DIR`)
4. an embedded `:/assets/…` resource, if one is ever added

| File | Use |
| --- | --- |
| `clivus-lockup-on-dark.png` | Welcome screen and About — the full mark + wordmark, drawn for the dark workspace |
| `clivus-mark.png` | Mark alone (white on transparent): places where the lockup is too wide, fallbacks |
| `clivus-icon.png` | 256 px app icon: window/taskbar icon at runtime |
| `clivus.ico` | Windows exe icon (16–256 px multi-size), referenced by `app/clivus.rc` |

The original asset set also contains a black lockup and a teal mark on transparent for light
backgrounds, plus icon PNGs at 16–1024 px; they are not needed at runtime and stay out of the repo.

If a file goes missing, the app still starts: it falls back to a built-in three-plane chevron mark
and the drawn wordmark (`app/branding.cpp`), so absence is a state, not a crash.

Palette reference: workspace `#14181c`, accent `#3d9ebe` (`app/style.h`).
