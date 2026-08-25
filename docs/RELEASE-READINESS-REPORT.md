# SourceGlo Pro — 1.0.0 Release Candidate Report

Date: 2026-08-25 · Version 1.0.0 RC

## What this milestone adds

Nothing new to hear — everything a retail release needs around the plugin:

- **Sanitizer gate** — the full 591-check suite rebuilt and run under
  ASan + UBSan (`make sanitize`), the pass that found the only real UB in
  EQGlo. Result recorded below.
- **Retail zip** — `make release` assembles
  `SourceGloPro-1.0.0-macOS.zip` (Install pkg + Read Me First + License),
  and *refuses* to package an unstapled installer so a Gatekeeper-blocked
  build can never ship by accident. Installer welcome/conclusion copy is
  release copy now — the "preview build / demonstration data" framing
  (stale since 0.9.1) is gone.
- **Marketing kit** — `marketing/`: SALES-PAGE.md, STORE-LISTINGS.txt
  (short/medium/bullets/requirements), five 1491×1055 screenshots + web
  JPEGs, matching the Bay2LA / Drum King / MasterGlo folder shape.
- **Windows CI, parked** — `CMakeLists.txt` (portable JUCE build, verified
  to configure against the local JUCE) and
  `.github/workflows/windows.yml` (vswhere generator discovery, pluginval
  strictness-5 gate, artifact upload), ported from MasterGlo with the
  per-target asset-destination fix that once shipped a Drum King VST3
  with no artwork. Activates the moment the repo has a GitHub remote.

## Release checklist

- [x] All features from the original handoff shipped (UI, engine, fix,
      library, presets, tabs)
- [x] make test: 591 checks, 0 failed
- [x] auval SUCCEEDED
- [x] make sanitize: see below
- [x] Universal binaries, signed
- [x] Installer signed (Developer ID Installer)
- [x] Retail zip target with staple gate
- [x] Marketing screenshots + copy
- [ ] **Notarisation** — needs the Apple credentials stored once:
      `xcrun notarytool store-credentials SourceGlo --apple-id ... --team-id 922D43C6FJ --password ...`
      then `make notarize && make release`
- [ ] **Windows build** — needs a GitHub remote; workflow is ready
- [ ] A human has opened it in a real DAW session and listened

## Sanitizer result

`make sanitize` (ASan + UBSan over the full JUCE tree and all plugin
sources, -O1): **591 checks, 0 failed, zero runtime errors, zero
AddressSanitizer reports.** The run compiled after the 1.0.0 bump, so it
doubles as the full suite pass for the release version.
