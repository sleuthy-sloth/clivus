# Clivus UI/UX Improvement Roadmap

Project: Clivus — DICOM Viewer
Scope: Usability, visual polish, interaction design, desktop quality, and comprehensive in-app documentation
Platform: Existing Qt-based desktop application
Primary goal: Turn Clivus from a functional, minimal DICOM viewer into a polished, approachable, dependable desktop application without sacrificing performance or expanding the scope unnecessarily.

---

## 1. Project Goals

### 1.1 Core objectives

* Make the primary viewing workflow understandable to first-time users.
* Make common image-viewing operations accessible through visible controls, menus, and keyboard shortcuts.
* Improve the series browser, image canvas, status information, and loading states.
* Add clear, editable window/level controls and useful viewing presets.
* Provide a complete Help menu with an in-app manual, keyboard reference, troubleshooting guide, and application information.
* Improve layout persistence, accessibility, error recovery, and behavior on different display sizes.
* Preserve existing asynchronous folder scanning, thumbnail generation, slice decoding, cancellation, and stale-result protection.
* Keep the code maintainable and consistent with the current Qt architecture.

### 1.2 Non-goals

These improvements are not intended to turn Clivus into a complete clinical workstation. Do not add the following solely as part of this UI/UX effort:

* 3D reconstruction, multiplanar reconstruction (MPR), or advanced segmentation.
* PACS connectivity or DICOM networking.
* Complex plugin systems.
* Large animated transitions or decorative visual effects.
* A wholesale migration to QML or another UI framework.
* Claims of diagnostic or clinical suitability without separate validation.

Keep the existing research-use limitation visible in the application and documentation.

---

## 2. Guiding Design Principles

### Usability

* Expose important operations as discoverable controls rather than relying exclusively on gestures.
* Use consistent names and icons across toolbars, menus, dialogs, and the manual.
* Provide feedback for loading, cancellation, selection, and errors.
* Preserve predictable keyboard and mouse behavior.
* Prefer progressive disclosure: show the most-used controls first, with advanced information available when needed.

### Visual design

* Use a dark, restrained workspace appropriate for viewing medical images.
* Keep the image viewport black or near-black to minimize distractions.
* Use charcoal or slate panels, subtle separators, and a single restrained accent color.
* Use consistent typography, icon sizing, control heights, and spacing.
* Avoid excessive borders, gradients, shadows, and decorative effects.
* Make selected, focused, disabled, loading, and error states visually distinct.

### Technical design

* Prefer native Qt widgets and actions.
* Use shared QAction instances for menu items, toolbar buttons, and shortcuts where practical.
* Keep UI operations on the UI thread and retain existing asynchronous processing patterns.
* Do not duplicate state between widgets when a single source of truth is available.
* Keep rendering, DICOM metadata, and UI presentation responsibilities separated.
* Do not silently invent missing metadata or imply that unsupported features are available.

---

## Phase 1 — Core Interface Polish

Objective: Make the existing application feel coherent, professional, and understandable before adding more advanced controls.

### 1.1 Main window and application layout

Review the existing main window and establish a consistent layout.

Tasks

* [ ] Establish a sensible minimum window size and test resizing.
* [ ] Add a compact primary toolbar beneath the menu bar.
* [ ] Standardize toolbar icon dimensions, spacing, and button sizing.
* [ ] Improve the series sidebar's spacing, padding, and typography.
* [ ] Improve selected-series highlighting and keyboard focus visibility.
* [ ] Add subtle separators between the toolbar, series browser, and image canvas.
* [ ] Ensure the canvas receives sufficient space when the window is resized.
* [ ] Provide a way to collapse or resize the series sidebar.
* [ ] Use consistent application terminology throughout the UI.
* [ ] Remove redundant or excessively verbose status text.

Proposed primary toolbar

Include the following actions where their behavior is supported:

1. Open Folder
2. Fit Image
3. Pan
4. Window/Level
5. Reset View

Each action should have a tooltip and, where appropriate, a keyboard shortcut. Actions that are not yet implemented should not be displayed as functional controls.

Keep the toolbar compact. Do not add every possible operation to the primary toolbar.

### 1.2 Menu organization

Organize the menu bar around common desktop application conventions.

File

* Open Folder
* Recent Folders, if implemented
* Close Current Study or Clear Study, if supported
* Exit

View

* Fit Image
* Reset View
* Toggle Series Sidebar
* Window/Level Presets
* Show Image Information, if implemented

Help

* User Manual
* Keyboard Shortcuts
* Troubleshooting
* About Clivus

Use separators to group related actions. Remove duplicate menu entries where they provide no benefit, but retain menu access to actions also available through the toolbar.

### 1.3 Series browser

Improve how users identify and select a series.

Tasks

* [ ] Display series descriptions prominently.
* [ ] Display modality, image count, and image dimensions where available.
* [ ] Retain thumbnails, but standardize their size and alignment.
* [ ] Make the selected series visually obvious.
* [ ] Provide a useful hover state and keyboard focus state.
* [ ] Handle long series descriptions through wrapping, truncation, or tooltips.
* [ ] Display a sensible fallback label when metadata is missing.
* [ ] Display an explicit message when a series cannot be previewed or decoded.
* [ ] Avoid showing unnecessary patient information in ordinary hover tooltips.
* [ ] Keep patient-identifying information out of logs and diagnostic messages unless explicitly needed and handled appropriately.

Example series entry

```
CT Chest

CT · 184 images · 512 × 512

[Thumbnail]
```

Metadata should be derived from the selected DICOM series. If a field is unavailable, omit it or display an explicit unavailable state rather than guessing.

### 1.4 Image canvas and information hierarchy

The image canvas should remain the visual focus of the application.

Tasks

* [ ] Keep the background black or near-black.
* [ ] Improve the appearance and positioning of image information overlays.
* [ ] Separate viewing information from diagnostic performance information.
* [ ] Use a consistent font and contrast level for overlays.
* [ ] Prevent overlays from obscuring important image content where practical.
* [ ] Confirm that image scaling remains correct when the window is resized.
* [ ] Ensure overlays update when the displayed series, slice, window/level, or zoom changes.

Proposed overlay layout

Top-left

* Series description
* Modality
* Image dimensions

Top-right

* Window center and width
* Zoom percentage

Bottom-left

* Slice position or index, when available
* Image orientation, only when supported by the underlying metadata

Bottom-right

* Additional relevant viewing information, if useful

Do not display unsupported orientation markers or infer anatomical orientation from assumptions.

### 1.5 Status bar and performance information

The current status area should communicate application state rather than displaying a long, continuously changing technical string.

Tasks

* [ ] Show concise status messages such as "Ready," "Loading series," and "Loading slice."
* [ ] Keep progress information visible during long-running operations.
* [ ] Keep the Cancel action available during cancellable work.
* [ ] Remove progress controls when they are no longer relevant.
* [ ] Move routine timing and FPS details to an optional diagnostics area.
* [ ] Keep detailed error information available without overwhelming the normal viewing experience.

Performance measurements are useful for development, but they should not dominate the interface for ordinary users.

### 1.6 Empty, loading, and error states

Create consistent states for common application situations.

Empty state

When no folder has been opened, display:

* A concise welcome message.
* A prominent Open Folder action.
* A short explanation of what Clivus does.
* A link or menu route to the User Manual.

Loading state

When scanning a folder or decoding a series:

* Display a concise progress message.
* Show a progress indicator when meaningful progress can be measured.
* Keep cancellation available where supported.
* Avoid blocking the main window.
* Prevent stale asynchronous results from replacing newer selections.

Error state

Provide clear messages for:

* An inaccessible or unreadable folder.
* A folder containing no supported DICOM images.
* An unsupported image encoding or pixel-data format.
* A failed thumbnail or slice decode.
* A cancelled operation.
* An unexpected internal error.

Each error should explain what happened, what the user can try next, and whether the rest of the application remains usable.

Do not expose raw exception text as the only user-facing explanation.

### 1.7 Phase 1 code areas

Review the existing code before editing. The following files are likely starting points, not an exhaustive or mandatory list:

* app/main_window.cpp
* app/main_window.h
* app/canvas_widget.cpp
* app/canvas_widget.h

Add new files only when they establish a clear responsibility, such as a reusable toolbar helper or shared style definitions.

### 1.8 Phase 1 acceptance criteria

Phase 1 is complete when:

* [ ] The application has a consistent toolbar, menu structure, and visual hierarchy.
* [ ] The series browser is easier to scan and understand.
* [ ] The image canvas presents key information without excessive clutter.
* [ ] Empty, loading, and error states are understandable.
* [ ] Progress and cancellation controls appear and disappear correctly.
* [ ] Performance diagnostics no longer dominate the normal interface.
* [ ] Existing asynchronous loading and cancellation behavior still works.
* [ ] The application builds and launches on all currently supported platforms.

---

## Phase 2 — Image Viewing Controls and Canvas Improvements

Objective: Make image navigation and adjustment discoverable, controllable, and consistent.

### 2.1 Formalize viewing interactions

Document and standardize the supported mouse and keyboard interactions.

**Current, verified interactions (from `app/canvas_widget.cpp` — verify again against HEAD before documenting):**

* **Left-button drag: window/level** (horizontal = width, vertical = center — Horos convention)
* **Right-button drag: pan**
* **Double-click: reset view**
* **Mouse wheel: change slices**
* **Ctrl + mouse wheel: zoom** (0.1×–20×)
* **R: reset the view** (verify)

The section below retains the original draft mappings; wherever they disagree with the verified list above, the verified list wins and the draft must be corrected.

The existing interactions include:

* Mouse wheel: change slices.
* Ctrl + mouse wheel: zoom.
* Right-button drag: adjust window width and level. *(Draft — verify; current code has window/level on LEFT drag)*
* Left-button drag: pan, with behavior potentially affected by annotation mode. *(Draft — verify; current code has pan on RIGHT drag)*
* Double-click: reset the view.
* A: toggle annotation mode. *(Does not exist yet — do not document)*
* R: reset the view.

Resolve any conflicts between annotation and pan behavior.

Tasks

* [ ] Create a single authoritative list of mouse and keyboard mappings.
* [ ] Expose important operations as toolbar and menu actions.
* [ ] Display the active tool in the UI.
* [ ] Add tooltips describing mouse gestures.
* [ ] Make the active interaction mode visually apparent.
* [ ] Ensure keyboard shortcuts work when focus is in the intended part of the application.
* [ ] Avoid overriding normal text-editing shortcuts inside input fields.
* [ ] Provide a reliable way to return to the default interaction mode.

### 2.2 Window/level controls

Window center and window width are central to grayscale DICOM viewing and should be easy to inspect and change.

Tasks

* [ ] Add editable Window Center and Window Width controls.
* [ ] Keep the controls synchronized with mouse-based adjustment.
* [ ] Validate typed values.
* [ ] Prevent invalid values from crashing the application or producing undefined rendering behavior.
* [ ] Clearly distinguish current values from preset defaults.
* [ ] Provide a way to restore the appropriate default values.
* [ ] Handle color images separately when window/level adjustment is unsupported or inapplicable (colour frames already ignore windowing — keep that behaviour explicit in the UI).

Use descriptive labels such as:

* Window Center
* Window Width

Do not rely on "WC" and "WW" alone in the interface. Abbreviations can be used in compact overlays with a corresponding tooltip.

Suggested interaction

1. The user selects a preset or enters numeric values.
2. The application validates the input.
3. The canvas updates using the accepted values.
4. The displayed values update to match the actual rendering state.
5. Invalid input produces a clear inline error or validation message.

Avoid creating multiple competing sources of truth for window/level state.

### 2.3 Window/level presets

Provide a compact preset selector.

Candidate presets include:

* Soft Tissue
* Lung
* Bone
* Brain
* Liver
* MRI Default

Requirements

* [ ] Verify each preset's center and width against appropriate reference values.
* [ ] Clearly identify presets as viewing conventions rather than guaranteed diagnostic settings.
* [ ] Make the active preset visible.
* [ ] Allow manual adjustments after selecting a preset.
* [ ] Indicate when manually adjusted values no longer match a named preset.
* [ ] Avoid presenting CT-specific presets as appropriate for every modality.
* [ ] Handle color images and unsupported modalities gracefully.

Preset names alone are not sufficient. Their numerical values, intended modality, and behavior should be documented in the manual.

### 2.4 Zoom and fit-to-window behavior

Tasks

* [ ] Implement or refine Fit Image.
* [ ] Implement or refine Reset View.
* [ ] Display the current zoom percentage.
* [ ] Keep zoom state synchronized with mouse-wheel and toolbar interactions.
* [ ] Define how zoom behaves when the user changes slices.
* [ ] Define how zoom behaves when switching series.
* [ ] Test zoom and pan after window resizing.
* [ ] Avoid accidental image distortion when fitting images of different dimensions.

Fit Image should scale the image to the available viewport while preserving the image's aspect ratio. Reset View should have a clearly documented, consistent behavior.

### 2.5 Slice navigation

Tasks

* [ ] Add a slice slider for multi-slice series.
* [ ] Display the current slice index and total slice count.
* [ ] Allow direct navigation where appropriate.
* [ ] Synchronize slider, mouse wheel, keyboard navigation, and displayed slice information.
* [ ] Handle single-image series by disabling or simplifying slice navigation.
* [ ] Handle irregular or missing slice-position metadata without inventing physical positions.
* [ ] Avoid excessive decoding requests during rapid scrolling.
* [ ] Keep the UI responsive during slice changes.
* [ ] Ensure a late result from an earlier request cannot replace the currently selected slice. (Stale-result protection already exists in the async decode pipeline — preserve it.)

Example:

```
Slice 24 / 184
```

If reliable physical slice-position information is available, the application may additionally display it. Do not substitute slice index for physical position without labeling it correctly.

### 2.6 Canvas interaction feedback

Tasks

* [ ] Change the cursor to reflect the active interaction mode.
* [ ] Show which tool is active.
* [ ] Display a subtle zoom or adjustment indicator when useful.
* [ ] Make pan and zoom behavior predictable.
* [ ] Prevent tool-state changes from leaving the application in an unexpected interaction mode.
* [ ] Ensure overlays do not intercept mouse interactions intended for the image.
* [ ] Keep image rendering smooth during interaction.

Avoid elaborate animations. Feedback should be immediate and lightweight.

### 2.7 Phase 2 acceptance criteria

Phase 2 is complete when:

* [ ] Window center and width can be inspected and edited.
* [ ] Presets are modality-aware and documented.
* [ ] Fit, reset, zoom, and pan behavior is consistent.
* [ ] Slice navigation supports both scrolling and direct selection.
* [ ] The canvas communicates the current viewing state.
* [ ] Color images are handled appropriately.
* [ ] Rapid navigation and series switching do not produce stale-image bugs.
* [ ] All controls and shortcuts are documented in the User Manual.

---

## Phase 3 — Desktop Quality, Accessibility, and Recovery

Objective: Make Clivus behave like a dependable desktop application during longer sessions and less common workflows.

### 3.1 Persistent layout and preferences

Tasks

* [ ] Remember the main window size and position.
* [ ] Remember the series sidebar width.
* [ ] Remember the sidebar's collapsed or expanded state.
* [ ] Restore sensible defaults if saved settings are missing or invalid.
* [ ] Persist appropriate non-sensitive UI preferences.
* [ ] Make preferences predictable across application restarts.
* [ ] Avoid persisting patient-identifying information or recently opened paths without an explicit product decision.

Use Qt's standard settings mechanisms where suitable. Do not store large image data, sensitive metadata, or unnecessary study information in UI settings.

### 3.2 Series search and navigation

Tasks

* [ ] Add series search or filtering if it materially helps with large studies.
* [ ] Search visible series metadata such as descriptions and modality.
* [ ] Make clearing the search simple.
* [ ] Keep the selected series visible when filtering allows it.
* [ ] Provide an obvious no-results state.
* [ ] Support keyboard navigation between series.
* [ ] Avoid rebuilding thumbnails unnecessarily during filtering.
* [ ] Ensure filtering does not interfere with background loading.

Search should be lightweight and should not require a database or a complex indexing subsystem.

### 3.3 Recent folders

If recent-folder support is added, keep it small and predictable.

Tasks

* [ ] Offer a short Recent Folders list.
* [ ] Handle folders that no longer exist.
* [ ] Remove inaccessible entries or clearly mark them as unavailable.
* [ ] Provide a way to clear the list.
* [ ] Avoid storing sensitive paths by default if that conflicts with the application's privacy goals.
* [ ] Document the behavior in the manual.

Recent folders are optional and should not delay more important accessibility or stability work.

### 3.4 Keyboard accessibility and focus behavior

Tasks

* [ ] Ensure toolbar actions are keyboard accessible.
* [ ] Ensure series selection can be controlled without a mouse.
* [ ] Make focus visible against the dark theme.
* [ ] Avoid shortcut conflicts.
* [ ] Use descriptive accessible names for icon-only controls.
* [ ] Ensure dialogs have sensible default and cancel actions.
* [ ] Make error messages accessible to keyboard and assistive-technology users.
* [ ] Test the interface at different display scaling settings.
* [ ] Confirm controls remain usable at narrow window widths.

Do not use color as the only way to communicate selected, active, disabled, or error states.

### 3.5 Error recovery and diagnostics

Tasks

* [ ] Standardize user-facing error messages.
* [ ] Distinguish recoverable errors from failures that require reopening the study or restarting the application.
* [ ] Offer actionable next steps where possible.
* [ ] Make cancellation reliably return the UI to a valid state.
* [ ] Ensure failed thumbnails do not prevent users from selecting other series.
* [ ] Ensure a failed slice decode does not leave progress indicators stuck.
* [ ] Keep diagnostic information separate from ordinary UI messaging.
* [ ] Avoid logging patient names, identifiers, or image contents by default.
* [ ] Avoid reporting successful completion before the operation has actually finished.

If logging is introduced or expanded, document where logs are stored, what they contain, and how users can share them safely.

### 3.6 Performance and resource handling

Tasks

* [ ] Test large directories containing many DICOM files.
* [ ] Test series with large image counts.
* [ ] Verify that cancellation releases resources correctly.
* [ ] Verify that changing series during decoding does not display stale content.
* [ ] Avoid unnecessary thumbnail regeneration.
* [ ] Avoid unnecessary widget recreation during updates.
* [ ] Test memory use during repeated series switching.
* [ ] Keep all expensive scanning and decoding work off the UI thread.
* [ ] Confirm progress indicators reflect real work rather than arbitrary timers.

Performance optimizations should be driven by observed bottlenecks rather than premature architectural changes.

### 3.7 Cross-platform and display testing

Tasks

* [ ] Test Windows at common resolutions and scaling levels.
* [ ] Test every other platform currently supported by the project.
* [ ] Test narrow and wide window sizes.
* [ ] Test high-DPI displays.
* [ ] Test keyboard navigation and focus order.
* [ ] Check for clipped labels, oversized icons, and misplaced dialogs.
* [ ] Confirm the dark theme remains legible in supported operating-system configurations.
* [ ] Confirm that all supported builds retain their existing dependencies and deployment requirements.

### 3.8 Phase 3 acceptance criteria

Phase 3 is complete when:

* [ ] The application restores the intended UI layout.
* [ ] Search and keyboard navigation work reliably if implemented.
* [ ] Recoverable errors do not leave the application in a broken state.
* [ ] Sensitive metadata is not unnecessarily exposed in tooltips, logs, or saved settings.
* [ ] The interface works at different resolutions and scaling levels.
* [ ] Large-series and repeated-navigation tests do not reveal major responsiveness or memory problems.
* [ ] Platform-specific regressions have been addressed.

---

## Phase 4 — Validation, Documentation, and Release Readiness

Objective: Validate the complete experience, publish the manual, and establish a repeatable quality-check process.

### 4.1 Functional regression testing

Build a repeatable checklist that covers the entire user workflow.

Folder and study handling

* [ ] Open a valid folder.
* [ ] Open an empty folder.
* [ ] Open a folder containing non-DICOM files.
* [ ] Open a folder containing mixed supported and unsupported files.
* [ ] Open an inaccessible folder.
* [ ] Cancel a scan.
* [ ] Reopen another folder after an error.

Series and image handling

* [ ] Select each series in a multi-series study.
* [ ] Display a single-image series.
* [ ] Display a multi-slice series.
* [ ] Handle missing optional metadata.
* [ ] Handle a series with a failed thumbnail.
* [ ] Handle a slice that cannot be decoded.
* [ ] Switch series while background work is in progress.
* [ ] Rapidly scroll through slices.
* [ ] Confirm the final displayed slice matches the current selection.

Image controls

* [ ] Use Fit Image.
* [ ] Use Reset View.
* [ ] Pan the image.
* [ ] Zoom in and out.
* [ ] Change window center and width using the mouse.
* [ ] Enter numeric window/level values.
* [ ] Apply each available preset.
* [ ] Verify color-image behavior.
* [ ] Navigate slices with the wheel, slider, and keyboard where supported.

Application state

* [ ] Resize the window.
* [ ] Collapse and expand the sidebar.
* [ ] Restart the application and verify saved preferences.
* [ ] Open and close Help.
* [ ] Use every documented keyboard shortcut.
* [ ] Confirm Cancel and Exit behave correctly during supported operations.

### 4.2 Visual regression review

Inspect screenshots at a consistent set of window sizes.

Review:

* Initial empty state.
* A folder being scanned.
* A selected series with thumbnails.
* A loaded image with overlays.
* A series with a long description.
* An error dialog.
* The window/level controls.
* The Help manual.
* The keyboard-shortcuts reference.
* The About dialog.

Look for:

* Inconsistent spacing.
* Misaligned text and icons.
* Poor contrast.
* Truncated controls.
* Unnecessary empty space.
* Overlays that compete with the image.
* Inconsistent active, disabled, and selected states.

Do not treat screenshots as a substitute for interactive testing.

### 4.3 Documentation and release review

Tasks

* [ ] Complete the in-app User Manual.
* [ ] Verify all shortcuts against the actual implementation.
* [ ] Verify all menu items against the actual implementation.
* [ ] Document supported image formats and known limitations accurately.
* [ ] Explain the research-use limitation.
* [ ] Update the README to summarize the features and direct users to the manual.
* [ ] Include build and launch instructions that match the current repository.
* [ ] Remove stale documentation and outdated screenshots.
* [ ] Confirm that the Help menu works in packaged builds.
* [ ] Test the manual without an internet connection.
* [ ] Review all user-facing messages for consistency.

### 4.4 Phase 4 acceptance criteria

Phase 4 is complete when:

* [ ] All critical workflows have been exercised.
* [ ] Existing functionality still works after UI changes.
* [ ] Visual review has been completed at multiple sizes.
* [ ] The in-app manual is complete and matches the application.
* [ ] The README and manual agree about available features.
* [ ] The application builds and launches on supported platforms.
* [ ] The research-use notice remains visible and accurate.
* [ ] Known limitations are documented rather than hidden.

---

## 5. Help Menu and Complete User Manual

Priority: Implement the Help infrastructure during Phase 1 and complete the documentation alongside Phases 2–4.

The manual should be available from the application itself. It should not require an account, internet connection, external website, or separate documentation installation.

### 5.1 Help menu structure

Create a Help menu with these entries:

1. Clivus User Manual
2. Keyboard Shortcuts
3. Troubleshooting
4. About Clivus

Use native Qt menu actions and keyboard shortcuts where appropriate.

Suggested shortcuts, subject to verification against existing mappings and platform conventions:

* User Manual: F1
* Keyboard Shortcuts: accessible from the Help menu
* About Clivus: accessible from the Help menu

Avoid assigning shortcuts that conflict with existing image interactions or operating-system conventions.

### 5.2 Recommended implementation

Implement the manual as a dedicated Qt dialog or window using Qt Widgets.

Suggested files:

* app/help_window.h
* app/help_window.cpp

Optional supporting files:

* docs/user_manual.md
* docs/keyboard_shortcuts.md
* docs/troubleshooting.md

Choose one authoritative source for manual content. Avoid maintaining separate copies of the same instructions in the application, README, and Markdown files.

Recommended presentation

Use a two-pane layout:

Left pane: navigation

* Getting Started
* Interface Overview
* Opening DICOM Files
* Working with Series
* Viewing Images
* Window/Level
* Keyboard and Mouse Controls
* Preferences
* Troubleshooting
* Limitations and Research Use
* About Clivus

Right pane: article

* Article title.
* Concise introductory explanation.
* Step-by-step instructions.
* Relevant screenshots or diagrams where helpful.
* Related topics and troubleshooting links.

Include a search field above the navigation tree or article list.

Requirements

* [ ] The Help window opens quickly.
* [ ] The manual is bundled with the application.
* [ ] The manual works offline.
* [ ] Navigation is keyboard accessible.
* [ ] Search filters or locates relevant manual topics.
* [ ] The window can be resized.
* [ ] The manual uses the application's typography and visual design.
* [ ] Links between manual sections work.
* [ ] The manual does not depend on remote web resources.
* [ ] Text remains readable at different display scaling settings.

A Qt text browser with bundled HTML generated from Markdown is one reasonable approach. A simple native text viewer is also acceptable if it supports useful navigation and search. Do not introduce a large documentation framework unless there is a concrete need.

### 5.3 Full manual content outline

The following sections define the minimum content for the manual.

**Chapter 1 — Welcome to Clivus.** What Clivus is; what tasks it supports; what DICOM is at a high level; who it is for; what it does not support; the distinction between viewing software and a clinically validated diagnostic workstation. Research-use notice prominently:

> Clivus is a DICOM viewing application intended for research and educational use. It is not a substitute for a clinically validated diagnostic workstation, and its output should not be relied upon for clinical decisions without appropriate independent validation.

Review against the project's intended legal and distribution requirements before release.

**Chapter 2 — Getting Started.** Launch; the empty state; opening a folder; what happens during scanning; how the series list populates; selecting a series; recognizing a loaded image; cancelling; accessing Help. A short first-use walkthrough matching actual behavior.

**Chapter 3 — Understanding the Interface.** Each component: menu bar, toolbar, series browser, thumbnails, canvas, overlays, window/level controls, slice navigation, status bar, progress indicator, cancel, Help window — what each does, when available, what happens on activation. Annotated screenshot once the design stabilizes. Never publish screenshots with real patient identifiers.

**Chapter 4 — Opening DICOM Files.** Folder selection; subfolder scanning behavior; supported file types and encodings; scanning progress; cancelling; no-supported-images outcome; partially unreadable folders; recovery from inaccessible-folder errors. Document actual scanning behavior.

**Chapter 5 — Understanding Studies and Series.** Study vs series vs slice; per-series information (modality, description, count, dimensions); selecting/identifying the active series; why a description might be blank; why a thumbnail may be unavailable; what to do when a series cannot load. Distinguish metadata reported by files from information inferred by the application.

**Chapter 6 — Viewing and Navigating Images.** Every supported navigation method: mouse/keyboard action, expected result, whether it changes the image or the viewport, limitations. Cover slice change, pan, zoom, fit, reset, series switch, reading overlays. Never document an unimplemented operation as a feature.

**Chapter 7 — Window Center and Window Width.** Window/level in accessible language: how values affect grayscale display; mouse adjustment; numeric entry; presets; returning to defaults; why the same image looks different under different settings. A preset table with values once verified — a draft skeleton:

| Preset | Intended use | Modality considerations |
|---|---|---|
| Soft Tissue | General soft-tissue viewing | Primarily CT |
| Lung | Lung parenchyma viewing | Primarily CT |
| Bone | Bone detail viewing | Primarily CT |
| Brain | Brain-focused CT viewing | Primarily CT |
| Liver | Liver-focused CT viewing | Primarily CT |
| MRI Default | Default viewing behavior for MRI | Must be defined and validated by the implementation |

Presets are viewing conveniences, not a substitute for image interpretation.

**Chapter 8 — Keyboard and Mouse Reference.** Compact table from the *verified* implementation — a draft to be corrected against `canvas_widget.cpp`:

| Action | Input | Availability |
|---|---|---|
| Change slices | Mouse wheel | Multi-slice series |
| Zoom | Ctrl + mouse wheel | Image canvas focused |
| Window/level | Left-button drag | Grayscale images |
| Pan | Right-button drag | Image canvas |
| Reset view | Double-click / R | Image canvas |
| Open User Manual | F1 | Application |

If the implementation changes, update the manual and shortcut tests together. Where practical, let users open a shortcut reference directly from the Help menu.

**Chapter 9 — Preferences and Layout.** Window resizing; sidebar resize/collapse; which settings persist; restoring defaults; recent folders; behavior when settings are unavailable/invalid. Do not describe unimplemented preferences.

**Chapter 10 — Troubleshooting.** Structured symptoms → explanations → next steps. Section skeletons: a folder will not open; a series appears empty or has no thumbnail; an image fails to load; the image is too dark or too bright; slice navigation does not work; the application appears busy; the image looks incorrectly oriented (do not infer orientation from assumptions); the interface layout looks incorrect; a problem persists (diagnostic checklist: version, OS, repro steps, exact message, known-good dataset, consistency). **Warn users not to attach real patient files or identifying screenshots to public bug reports.**

**Chapter 11 — Limitations and Supported Features.** Version-specific: supported encodings, pixel-data types, modalities, multi-frame behavior, metadata/orientation limitations, unsupported features, performance limits — populated from verified implementation and testing. Do not promise broad DICOM compatibility from the extension name.

**Chapter 12 — Privacy and Research Use.** Whether files are read locally; whether anything transmits over the network; whether logs are created; whether recent folders are remembered; whether patient names/identifiers can appear in the UI; how users can avoid exposing identifying information in screenshots and bug reports. Only make privacy claims verified against the code and dependencies.

**Chapter 13 — About Clivus.** Name, version, build/commit, repository link, license information, third-party acknowledgments, research-use limitation — consistent with the application's design.

### 5.4 Manual quality requirements

* [ ] Every instruction matches the current UI.
* [ ] Every shortcut matches the current code.
* [ ] Every documented preset has verified behavior.
* [ ] Unsupported features are explicitly identified.
* [ ] Troubleshooting advice has been checked against actual error handling.
* [ ] The manual works without network access.
* [ ] Search returns relevant topics.
* [ ] Screenshots contain synthetic or properly de-identified test data.
* [ ] The manual is usable at high display scaling.
* [ ] The Help menu and manual are tested in packaged builds.
* [ ] Documentation changes are reviewed whenever related UI behavior changes.

---

## 6. Suggested Implementation Architecture

Use the existing architecture as the starting point and make changes only after reviewing the current code.

### 6.1 Main window

MainWindow is responsible for: menu bar; primary toolbar; series browser; main layout; status and progress display; wiring actions to the viewer and loading workflow; opening Help and About windows. Avoid placing all styling, manual content, image rendering, and DICOM decoding logic inside the main-window implementation.

### 6.2 Canvas widget

CanvasWidget is responsible for: rendering the current image; image scaling and viewport behavior; pan and zoom interaction; window/level interaction; image overlays; cursor and active-tool feedback. Expose only the state and operations needed by the main window. Avoid having the main window inspect internal rendering details directly.

### 6.3 Shared actions and state

Define actions once and reuse them in menus and toolbars; keep keyboard shortcuts tied to the same actions as visible controls; keep window/level values synchronized between controls and rendering; keep slice navigation synchronized between the slider, keyboard, mouse, and canvas; use a consistent approach to enabling or disabling controls during loading. Do not create multiple independent implementations of the same command.

### 6.4 Styling

Prefer a small, shared set of Qt styles or theme definitions over scattered, conflicting style rules. Define consistent values for: panel background; canvas background; primary/secondary text; borders and separators; accent color; selected, hover, and focus states; error and warning states; standard spacing; toolbar and icon dimensions. Test under the supported operating systems before committing to platform-specific styling.

### 6.5 Asynchronous work

Preserve the existing background-processing architecture for folder scanning, thumbnail generation, slice decoding, cancellation, and stale-result protection. New UI controls should interact with this architecture rather than introducing a second loading pipeline. Rapid series switching and slice scrolling must not allow a late result from an earlier request to overwrite the current selection.

---

## 7. Recommended Delivery Order

Implement the roadmap in small, independently testable changes.

| Order | Deliverable | Dependencies |
|---|---|---|
| 1 | Shared visual conventions and toolbar | Existing main window |
| 2 | Series browser polish | Existing series metadata |
| 3 | Canvas overlays and status cleanup | Canvas and metadata state |
| 4 | Empty, loading, and error states | Existing loading workflow |
| 5 | Help menu and manual window skeleton | Main window |
| 6 | Numeric window/level controls and presets | Rendering state |
| 7 | Slice navigation and viewport controls | Canvas state |
| 8 | Layout persistence and keyboard accessibility | Stable interface |
| 9 | Manual completion and troubleshooting | Verified UI behavior |
| 10 | Regression testing and release review | All prior work |

The Help window skeleton should arrive early, while its detailed chapters can be completed as the associated features stabilize.

---

## 8. Definition of Done

The overall project is complete when all of the following are true:

* [ ] The interface has a consistent visual design.
* [ ] Common operations are discoverable and accessible through visible controls.
* [ ] The series browser is easy to scan and navigate.
* [ ] Image information is organized and readable.
* [ ] Window/level controls are synchronized and validated.
* [ ] Image navigation and viewport controls behave consistently.
* [ ] Loading, cancellation, and error handling are clear.
* [ ] Saved layout preferences work reliably.
* [ ] Keyboard navigation and focus states are usable.
* [ ] The application remains responsive during background operations.
* [ ] No major regressions are present in supported DICOM workflows.
* [ ] The Help menu opens a complete, searchable, offline manual.
* [ ] The manual matches the shipped application.
* [ ] Privacy and research-use messaging is accurate.
* [ ] The application builds and launches on supported platforms.

---

## 9. Instructions for the Coding Agent

1. **Inspect before editing.** Review the current branch, project structure, build configuration, and relevant code before making changes.
2. **Work in phases.** Complete and validate one phase before moving to the next.
3. **Prefer incremental changes.** Do not rewrite functioning rendering or asynchronous loading systems merely to simplify the UI.
4. **Verify actual behavior.** Inspect existing shortcuts, supported formats, window/level presets, and loading behavior before documenting them.
5. **Keep changes scoped.** Do not introduce unrelated features or large new dependencies without explaining their value.
6. **Build and test.** Run the available build and test commands after each meaningful change.
7. **Test interaction paths.** Check rapid series switching, cancellation, resizing, keyboard navigation, and error recovery — not only successful startup.
8. **Keep documentation synchronized.** Update the manual and README when user-visible behavior changes.
9. **Protect patient privacy.** Use synthetic or de-identified data for screenshots and tests; avoid unnecessary patient metadata in logs or tooltips.
10. **Report honestly.** Summarize files changed, functionality implemented, tests run, failures encountered, and any incomplete work. Do not claim a feature or test is complete when it has not been verified.

**Final priority:** deliver a polished, dependable core viewer first; then make its image controls easier to use; then improve desktop quality; then complete the in-app manual. The intended result is a focused DICOM viewer with professional desktop ergonomics and clear documentation — not a larger application weighed down by features the project does not yet need.
