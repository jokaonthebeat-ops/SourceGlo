# SourceGlo Pro UI Assets Pack v1.1

## Start here

This package converts the approved SourceGlo Pro mockup into a production handoff for a JUCE plugin project. The approved reference image is the visual authority. Claude must reproduce the same layout, spacing, panel hierarchy, cyan/gold accent balance, and premium control treatment—not redesign it.

### Design coordinate system

- Base width: **1491 px**
- Base height: **1055 px**
- Aspect ratio: **1.4133:1**
- Recommended initial plugin size: **1491 × 1055**
- Recommended minimum size: **1044 × 739** at 70% uniform scale
- Resizing method: preserve aspect ratio and scale the entire coordinate system uniformly. Do not reflow or move panels at smaller sizes.

### Which files matter most

1. `00_REFERENCE/SourceGlo_Pro_Approved_UI_1491x1055.png` — final visual authority.
2. `02_BASE/sourceglo_shell_1491x1055.png` — reusable chassis and panel shell.
3. `08_LAYOUT/layout_1491x1055.json` — exact primary bounds.
4. `08_LAYOUT/control_map.csv` — control/component map.
5. `09_JUCE_HANDOFF/CLAUDE_MASTER_BUILD_PROMPT.md` — paste into Claude with the complete folder attached.
6. `09_JUCE_HANDOFF/JUCE_IMPLEMENTATION_SPEC.md` — engineering rules.
7. `09_JUCE_HANDOFF/QA_ACCEPTANCE_CRITERIA.md` — visual approval checklist.

### Rendering strategy

Use a hybrid approach:

- Load the shell/background and nonchanging texture assets as images.
- Render all labels, numbers, meters, analyzers, status values, lists, and controls in JUCE so they remain sharp and dynamic.
- Use the supplied filmstrip knobs, button state graphics, cards, icons, and HUD assets.
- Do not use stock JUCE rotary sliders, default combo boxes, generic gradients, or mismatched third-party control art.

### Folder summary

- `00_REFERENCE` — approved mockup, annotated layout, wireframe.
- `01_BRAND` — premium SourceGlo Pro logo lockups and emblem marks; all former logo files were removed.
- `02_BASE` — shell, panels, dividers, and texture.
- `03_HUD` — score ring, pod, and status assets.
- `04_CONTROLS` — knob filmstrips, buttons, toggles, and meter parts.
- `05_CARDS` — diagnostic and rescue list backgrounds.
- `06_ANALYZERS` — spectrum and radar grids.
- `07_ICONS` — original scalable SVG icons.
- `08_LAYOUT` — layout, colors, typography, parameters, and control data.
- `09_JUCE_HANDOFF` — Claude prompt, JUCE notes, QA, and starter headers.
- `10_PREVIEWS` — visual contact sheets.

### Important

The reference mockup contains sample values and example file names. They are not hardcoded final product behavior. Replace them with live analysis data while preserving the exact visual treatment.


### v1.1 brand update

The original simple SourceGlo logo and mark are no longer included. The approved reference, annotated layout, preview sheet, manifest, and Claude handoff now point only to the premium metallic SourceGlo Pro identity.
