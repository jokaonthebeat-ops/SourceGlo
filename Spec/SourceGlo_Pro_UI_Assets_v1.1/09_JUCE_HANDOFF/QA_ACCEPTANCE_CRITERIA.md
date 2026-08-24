# SourceGlo Pro UI Acceptance Checklist

## Visual identity

- [ ] The plugin opens with the exact approved panel hierarchy.
- [ ] The new premium SourceGlo Pro logo asset is used at x=19, y=16, w=320, h=42; no former logo or font-built substitute appears anywhere.
- [ ] Cyan remains the primary analysis color and gold remains the action/warning accent.
- [ ] No generic stock JUCE widgets are visible.
- [ ] Knobs match the supplied filmstrip art and do not look flat or cheap.

## Geometry at 1491 × 1055

- [ ] Header outer bounds align with the reference.
- [ ] Left Source panel aligns from y=75 through the macro/footer boundary.
- [ ] Main Source Score HUD is centered at the approved location.
- [ ] Tone, Punch, Level, Phase, and Fit pods align around the score ring.
- [ ] Analyze, Fix Source, and A/B buttons match approved widths and spacing.
- [ ] Diagnostics panel aligns to the right edge with four visible cards.
- [ ] Lower tabs align and the active cyan underline matches the reference.
- [ ] Spectrum, Masking/Fit, Macro, and Rescue regions match the control map.
- [ ] Eight macro knobs are evenly spaced and vertically aligned.
- [ ] Five rescue rows and Browse Library button align with the reference.
- [ ] Footer controls and center tagline align with the reference.

## Text and data

- [ ] All live text is rendered in code and remains sharp.
- [ ] No accidental AI-generated misspellings from the reference image remain.
- [ ] Section headings use the specified uppercase hierarchy.
- [ ] Numeric values have consistent alignment and units.
- [ ] Labels do not overlap at 70%, 100%, 125%, and 150% UI scale.

## Interaction

- [ ] Buttons have visible hover and pressed states.
- [ ] Tabs switch content without shifting the layout.
- [ ] Knob drag behavior is consistent and supports fine adjustment.
- [ ] Combo boxes open reliably and inherit the custom look.
- [ ] Rescue rows can be selected, previewed, and favorited.
- [ ] Tooltips and accessible names are present.

## Performance

- [ ] No allocation or file access occurs on the audio thread.
- [ ] Analyzer and meter repaints do not repaint the whole editor.
- [ ] UI remains smooth while audio is running.
- [ ] Static SVG/PNG assets are cached.
- [ ] Editor resize does not blur or distort the UI.

## Final comparison

- [ ] A 1491 × 1055 screenshot has been overlaid against the approved mockup at 50% opacity.
- [ ] All major component edges align within approximately 2–4 pixels.
- [ ] Any intentional differences have been documented before approval.
