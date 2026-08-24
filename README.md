# SourceGlo Pro

Production-intelligence audio plugin for Diamond Loopz: analyzes a source
(kick, 808, snare, vocal...), scores it against a modern pro standard,
diagnoses problems, and will suggest fixes and rescue samples.

- `Spec/` — the supplied asset pack + Claude handoff, preserved verbatim (the product spec).
- `Assets/` — runtime artwork, shipped into each bundle at `Contents/Resources/Assets`.
- `Source/` — JUCE plugin source (hand-rolled Makefile build; this machine is CLT-only).
- `tools/` — headless UI shot, deterministic test suite, VST3 probe, PNG validator.
- `docs/UI-MILESTONE-REPORT.md` — current status and documented differences vs the approved mockup.

Build: `make -j 2` (never higher on this machine), `make test`, `make uishot`,
`make install`. See the Makefile header for all targets.
