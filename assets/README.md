# Brand assets

Drop the Clivus mark here as **`clivus-mark.svg`** (preferred) or **`clivus-mark.png`**.

The app looks for it, in order:

1. `assets/clivus-mark.svg` / `.png` next to the packaged executable
2. `../assets/…` (build-tree runs)
3. this folder in a source checkout (development runs)
4. an embedded `:/assets/…` resource, if one is ever added

No code change is needed: the welcome screen and the About dialog pick the file up on the next
launch. Until it exists they draw the built-in fallback mark (three stacked chevrons) plus the
CLIVUS wordmark, so a build without artwork still looks intentional.

Suggested sizes — a single square SVG scales to all of these:

| Use | Rendered at |
| --- | --- |
| Welcome screen | 132 px |
| About dialog | 96 px |
| Window / taskbar icon | 16–256 px (provide a multi-size `.ico` for Windows if you want it crisp at small sizes) |

Keep the mark legible against the dark workspace: the app's background is near-black (`#14181c`),
and the single accent colour is a restrained steel teal (`#3d9ebe`, see `app/style.h`).
