# BinaryData Asset Map

Add the contents of folders `01_BRAND` through `07_ICONS` to the JUCE binary-data target. Use stable aliases in an asset cache instead of referring to generated BinaryData symbol names throughout the UI.

Recommended cache keys:

- shell
- logoHeader1x = `sourceglo_pro_premium_logo_header_320x42.png`
- logoHeader2x = `sourceglo_pro_premium_logo_header_640x84.png`
- logoHeader4x = `sourceglo_pro_premium_logo_header_1280x168.png`
- premiumMark512 = `sourceglo_premium_mark_512.png`
- scoreRingBase
- metricPodCyan / Red / Green / Gold
- macroKnobFilmstrip
- trimKnobFilmstrip
- buttonCyan[4]
- buttonGold[4]
- diagnosticHigh / Medium / Good
- rescueNormal / Hover / Selected
- spectrumGrid
- radarGrid
- icons[name]
