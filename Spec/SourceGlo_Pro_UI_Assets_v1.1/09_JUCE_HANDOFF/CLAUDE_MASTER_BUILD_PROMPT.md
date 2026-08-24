# CLAUDE MASTER BUILD PROMPT — SOURCEGLO PRO UI

You are building the JUCE user interface for **SourceGlo Pro**, a flagship production-intelligence audio plugin. The attached assets package is the complete visual handoff.

## Non-negotiable visual instruction

Reproduce the approved UI reference exactly in structure and appearance. **Do not redesign, simplify, modernize, rearrange, or substitute generic JUCE controls.** The file `00_REFERENCE/SourceGlo_Pro_Approved_UI_1491x1055.png` is the final visual authority.

The expected result must look like the approved mockup at first launch before any DSP is connected. All major panels, spacing, sizes, typography hierarchy, score HUD, diagnostic cards, tabs, analyzers, rescue list, macro knobs, meters, and footer must be present.

## Project assumptions

- Framework: JUCE 7 or JUCE 8.
- Plugin formats: VST3, AU, and Standalone on macOS; VST3 and Standalone on Windows.
- UI base coordinate system: **1491 × 1055**.
- UI must resize uniformly with aspect ratio locked at **1491:1055**.
- Use an `AudioProcessorValueTreeState` for persistent parameters.
- Use the included assets through BinaryData or a CMake binary-data target.
- Do not create cloud dependencies.

## Required first milestone

Build the complete UI shell and controls with placeholder analysis data. At this milestone:

- The plugin opens at 1491 × 1055.
- It visually matches the reference.
- Every button, tab, selector, knob, toggle, and list row responds visually.
- The score HUD displays placeholder data.
- Spectrum and radar sections animate from test data.
- Input/output meters move from processor meter values or test data.
- No DSP correction logic is required yet.

## Mandatory asset and layout usage

1. Load `02_BASE/sourceglo_shell_1491x1055.png` as the static chassis background.
2. Use `08_LAYOUT/layout_1491x1055.json` and `control_map.csv` for component bounds.
3. Use `04_CONTROLS/knobs/macro_knob_96px_128frames.png` for the eight lower macro knobs.
4. Use `04_CONTROLS/knobs/trim_knob_52px_128frames.png` for input/output trims.
5. Use the button state assets for Analyze, Fix Source, A/B, Browse Library, small switches, and tabs.
6. Use the supplied diagnostics and rescue card assets rather than generic rounded rectangles.
7. Use the supplied SVG icons. Do not replace them with emoji, text symbols, or unrelated icon packs.
8. Use the HUD ring base and draw live progress arcs above it.
9. Render live labels and values in code; do not bake changing data into the static background.

## Layout architecture

Create these child components:

- `HeaderComponent`
- `SourcePanelComponent`
- `SourceScoreHUD`
- `DiagnosticsPanelComponent`
- `AnalysisTabsComponent`
- `SpectrumAnalyzerComponent`
- `MaskingFitComponent`
- `MacroBankComponent`
- `RescueSuggestionsComponent`
- `FooterStatusComponent`

The editor owns them and positions each child from the approved 1491 × 1055 coordinate map.

Use a single uniform scale factor:

```cpp
const float sx = getWidth()  / 1491.0f;
const float sy = getHeight() / 1055.0f;
const float scale = juce::jmin (sx, sy);
```

Center the scaled design if the host supplies extra room. Do not independently stretch X and Y.

## Typography

Use installed/system fonts only. Preferred font family order:

1. Inter Display
2. Inter
3. SF Pro Display on macOS
4. Segoe UI on Windows
5. Arial fallback

Do not bundle font files.

Typography hierarchy is defined in `08_LAYOUT/typography.md`. Text must be rendered with subpixel antialiasing where supported. Use uppercase tracking for section titles and tabs.

## Visual style rules

- Dark graphite/black chassis.
- Cyan is the main intelligent-analysis accent.
- Gold is used for Fix Source, warning emphasis, and score progress.
- Red indicates high-severity faults.
- Amber indicates medium warnings.
- Green indicates clean/passed states.
- Shadows and glows must be restrained and premium—not blurry neon.
- Panel borders are thin and low contrast.
- Avoid flat gray default widgets.
- Controls must feel metallic/glass, matching the supplied artwork.

## Header behavior

- Premium logo at left. Use `01_BRAND/sourceglo_pro_premium_logo_header_320x42.png` at exactly x=19, y=16, w=320, h=42 on the base canvas. Do not draw the name with text and do not use any prior SourceGlo logo.
- Preset previous/next buttons.
- Central preset name field.
- Save, Save As, Undo, Redo, Settings, Help, and Power/Bypass icons.
- Preset field and utility buttons must have hover/down states.

## Source panel behavior

- Source Type selector with Auto and instrument categories.
- Input routing label and input meter.
- Input gain trim knob.
- Phase invert button.
- Output label and output meter.
- Output trim knob.
- Mono button.
- Source stats list: Peak, RMS, Crest Factor, True Peak, Duration, Tempo, and Key.
- Stats update at a controlled rate such as 10–20 Hz; do not repaint the entire editor at audio rate.

## Source Score HUD behavior

- Large center score from 0–100.
- Status phrase based on score:
  - 85–100: READY
  - 70–84: GOOD
  - 50–69: NEEDS WORK
  - 0–49: FIX REQUIRED
- Five pods: Tone, Punch, Level, Phase, Fit.
- Use color state by value, but preserve the approved cyan/gold visual hierarchy.
- Animate values using smoothing; no abrupt jumps.
- Buttons beneath HUD: Analyze, Fix Source, A/B.

## Diagnostics behavior

Render four visible diagnostic cards at a time. Each card includes:

- state icon
- title
- one- or two-line description
- severity badge
- disclosure chevron

States:

- high → red
- medium → amber/gold
- good → green

Cards are data-driven. Use the included card background assets.

## Analysis tabs

Tabs: Analyze, Fit, Rescue, Detail, Library.

- Active tab uses cyan text and the cyan underline.
- Inactive tabs use muted white.
- Preserve exact tab widths and positions from the reference.
- Switching tabs must not resize or shift the panel.

## Spectrum analyzer

- Log-frequency X axis from 20 Hz to 20 kHz.
- dB Y axis from +12 to -60 dB.
- Source trace in cyan.
- Reference trace in dashed gray.
- Optional red conflict region with title and frequency range.
- Pre/Post selector at upper right.
- Use a ring buffer and FFT data prepared outside paint where practical.
- Repaint around 30–45 Hz, not audio rate.

## Masking/Fit view

- Radar plot with Source in cyan and Mix Target in gold/dashed.
- Fit Score numeric display.
- Five horizontal range bars: Sub, Low, Low Mid, High Mid, High.
- Overlay and Delta view buttons.

## Macro bank

Eight controls in this exact order:

1. Punch
2. Body
3. Tone
4. Air
5. Stereo
6. Transients
7. Saturate
8. Output

Use the supplied filmstrip knob. Labels appear above and values below. Do not use generic vector knobs.

## Rescue panel

- Header: Rescue Suggestions, help icon, Auto Match toggle.
- Five visible recommendation rows.
- Each row: play button, waveform thumbnail, file name, two short descriptors, fit percentage, FIT label, favorite star.
- Selected row uses cyan outline.
- Browse Library button at bottom.
- List data can be placeholder for the UI milestone.

## Parameters

Create APVTS parameters from `08_LAYOUT/parameters.json`. Trigger-style actions such as Analyze and Fix Source should be command callbacks, not automatable continuous parameters unless the project already has a command architecture.

## Performance requirements

- Editor repaint should stay smooth at 60 FPS on a typical modern system.
- Cache static images and SVG drawables.
- Do not parse SVGs every paint call.
- Use image resampling quality appropriate to the current UI scale.
- Analyzer drawing must allocate no memory in the audio thread.
- No locks on the audio thread.
- Reduce analyzer repaint rate when the editor is hidden.

## Accessibility and host behavior

- Every interactive component needs a tooltip and accessible name.
- Keyboard focus should follow a logical order.
- Store the last UI scale in plugin state.
- The power button must bypass safely without popping.

## Development debug modes

Add a compile-time or hidden debug toggle that can show:

- component bounds
- base coordinates
- current scale
- repaint regions
- FPS

This is required so the finished editor can be overlaid against the approved screenshot.

## Visual QA requirement

After the UI is built, capture it at exactly 1491 × 1055 and compare it with the approved mockup at 50% opacity. Adjust bounds until panel edges, score HUD center, controls, tabs, analyzer regions, and rescue rows align. Do not call the UI finished while visible layout drift remains.

Use `09_JUCE_HANDOFF/QA_ACCEPTANCE_CRITERIA.md` as the completion checklist.

## Deliverables from Claude

1. Updated JUCE source files.
2. APVTS parameter layout.
3. Asset loader/caching code.
4. Custom components listed above.
5. A screenshot of the working UI at 1491 × 1055.
6. A short list of any visual differences that could not be matched, with exact reasons.

Begin with the UI shell and component hierarchy. Do not start advanced DSP until the visual milestone is approved.
