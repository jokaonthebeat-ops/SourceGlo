# SourceGlo Pro — UI Milestone Report

Date: 2026-08-24 · Version 0.9.0 (UI milestone) · Formats: VST3, AU, Standalone (macOS x86_64, signed)

## Status

The complete UI shell from `Spec/SourceGlo_Pro_UI_Assets_v1.1` is built and
passes the pack's first-milestone bar:

- Opens at 1491 × 1055 and visually matches `00_REFERENCE/SourceGlo_Pro_Approved_UI_1491x1055.png`.
- Every button, tab, selector, knob, toggle and list row responds visually
  (hover / down / disabled states from the supplied art).
- Score HUD, diagnostics, rescue list and source stats display placeholder
  model data that mirrors the approved mockup exactly.
- Spectrum runs a real FFT off the audio thread (lock-free FIFO) and falls
  back to animated deterministic test data when idle; the radar breathes on
  test data; meters run from processor taps.
- All 17 parameters from `08_LAYOUT/parameters.json` exist in the APVTS —
  verified against the JSON at runtime by the test suite.
- Uniform aspect-locked resize 70–150 % (single AffineTransform, no X/Y
  stretch), scale persisted in plugin state, settings menu + footer slider.
- Analyze / Fix Source are command callbacks, not parameters.
- Debug overlay (bounds, base grid, scale, FPS) on Cmd/Ctrl+Shift+D.

Verification: `make test` = 207 checks, 0 failed (artwork loads + filmstrip
slicing, parameter contract vs spec JSON, gain/phase/mono/bypass audio with
absolute targets, state round-trip, 25× editor open/close, accessibility
names). `auval -v aufx SGPr DmLz` SUCCEEDED. `make uishot` reports zero asset
load failures at def/min/max sizes.

## Differences from the approved reference, with reasons

1. **Layout JSON vs mockup drift.** `layout_1491x1055.json` places several
   clusters 6–36 px away from where the approved mockup draws them (preset
   cluster +28 px, action buttons ~+15 px, pods +6–8 px, rescue list +36 x /
   +20 y, diagnostics card content indent +20 px). The mockup is the declared
   visual authority, so the build matches the mockup; panel/primary bounds
   still follow the JSON where the two agree.
2. **Macro strip geometry.** The mockup's macro row spans the full width under
   both the source panel and the lower main panel (MACROS label at x≈33,
   knobs 110→954), not the JSON's `macros_panel` rect. Knob centres match the
   mockup, whose spacing is **not** mathematically even (steps 110–134 px) —
   the QA sheet's "evenly spaced" line is superseded by "matches the
   reference". Value readouts sit at y≈976–992, riding the footer well's top
   edge exactly as the mockup draws them.
3. **Spectrum axis labels** sit just below the `spectrum_panel` rect (as in
   the mockup); the component extends past the JSON rect to keep them.
4. **Filmstrip art is rotated +90° from its own spec.** `filmstrip_spec.md`
   says frame 0 = −135°; the shipped strips have frame 0 = −45° and the bezel
   specular lit from the wrong side. Frames are rotated back 90° at load
   (lossless pixel transpose). If the strips are ever re-exported per spec,
   remove `rotateFrameAnticlockwise` in `Source/Assets.cpp`.
5. **Score ring export margin.** The ring art is 445 × 445 centred in a
   1024 canvas; it is cropped to opaque bounds at load (same trap as the
   MasterGlo/Drum King logos), else it draws at ~40 % size.
6. **Status pill art carries no text** — the phrase is drawn live on top,
   which also satisfies "render live labels in code".
7. **Gold score arc fills from the +135° end backwards** (top → clockwise →
   bottom-right), matching the mockup; the HUD notes imply filling from
   −135°, which puts the gold on the wrong side.
8. **Tabs are equal-width** (5 × 137 px); the mockup's tab text spacing is
   slightly irregular (±15 px). Equal division keeps the underline and hit
   targets consistent.
9. **Sample values are live placeholders.** Output trim shows the parameter
   default 0.0 dB (mockup sample −1.0 dB); preset name, input routing, score,
   pods, stats, diagnostics and rescue rows are placeholder model data
   mirroring the mockup until the analysis engine lands. Per
   `README_START_HERE.md` these were never hardcoded product behaviour.
10. **Macro OUTPUT = `outputGain`.** `parameters.json` defines no separate
    macro output parameter, so the 8th macro knob and the source panel's
    output trim are two views of the same parameter.

## Not in this milestone (by design)

Analysis/scoring engine, Fix Source DSP, macro DSP, rescue library scanning
and preview audio, preset bank, Fit/Rescue/Detail/Library tab content (styled
placeholder panes today), Windows build, notarised installer.

## Verification commands

```
make test                                  # asset + parameter + audio + editor suite
make uishot ARGS="out.png def signal"      # 1491x1055 render with live test audio
make uishot ARGS="out.png min|max signal"  # 70 % / 150 % scale QA
auval -v aufx SGPr DmLz                    # after make install
```
