# Pixel pointer evidence

Backlog item from the review's Release 1 list: "Pixel pointer showing image row/column, stored value,
modality value/HU, and unit; suppress misleading calibrated units when metadata is absent."
Shipped in `c16f1d5`.

## What it shows

A permanent status-bar readout: `r<row> c<col>   <stored>   <modality> <unit>`.

- **Row/column are 1-based**, as DICOM counts pixels, and come from inverting the render mapping —
  the same `mapToFrame` the measurement tools use, so the numbers cannot disagree with what is drawn.
- **The value is the frame's own sample**, read from `ImageFrame::pixels` (modality units). It is not
  recovered from the display texture, which would be a quantised, windowed, 8-bit number.
- **Stored value** is shown only when the modality transform is invertible.
- **Unit** is shown only when the object supplies one.

## The three honesty rules, and why they needed core changes

`ImageFrame` now carries `rescaleSlope`, `rescaleIntercept`, `rescaleKnown` and `unit`, filled in by
the loader:

| Case | What the readout shows | Why |
|---|---|---|
| CT with `RescaleSlope`/`RescaleIntercept` | stored, modality, unit `HU` | the pair is present, so both numbers are real and HU is earned |
| RT Dose with `DoseGridScaling` | stored, modality, unit `Gy` | same reasoning, different unit |
| Object with a non-linear LUT | modality only, no unit | the LUT mapping cannot be inverted, so a "stored" number would be fabricated |
| Object with no rescale pair label | stored == modality, no unit | the identity transform is fine; claiming HU or any unit would be a guess |
| Colour pixel | first channel, unit literal `RGB` | there is no single greyscale value to report |

That last-but-one row is the one the review cared about: a number with the wrong unit attached is
worse than a number with no unit, and a viewer that prints "HU" for an MR is lying about the data.

## Tests

`app/test_canvas_view.cpp`:

- a point derived from `frameToWidget(10.5, 20.5)` reads back as row 21, column 11 — testing the
  inverse pair rather than my arithmetic about the fit;
- the value equals the frame's own sample at that index;
- with an identity transform, stored and modality agree;
- a frame with no rescale pair reports an empty unit;
- a point outside the image reports nothing.

`core/tests/test_scan.cpp`:

- loading `CT_small.dcm` gives unit `HU` and `rescaleKnown`;
- loading `MR_small.dcm` gives no unit.

With the unit assignment disabled: `FAIL a CT carrying a rescale pair reports HU`.

## Not done

- **Invert grayscale** (the companion backlog item) is not implemented.
- The readout is not in the canvas overlay; it sits in the status bar, separate from the slice text so
  the two cannot overwrite each other. Where it belongs on a radiologist's screen is a judgement I
  cannot make from here — worth asking when the next round of feedback comes.
- Nothing here was checked on a real display at real DPI.
