# SourceGlo Pro — Preset Bank Milestone Report

Date: 2026-08-25 · Version 0.9.4 · Follows the rescue-library milestone

## The header preset system is real

- **29 factory presets** across Kick / 808 / Snare / Clap / Bass / Hat /
  Percussion / Loop / Melody / Vocal / Utility, each a musical starting
  point for this catalogue. "Punchy Kick Starter" equals the parameter
  defaults plus source type Kick, so a fresh instance opens on it clean -
  exactly the approved mockup's header.
- **Click the preset name** for the browser (grouped by category, current
  ticked); **prev/next** step with wrap.
- **SAVE / SAVE AS** — user presets live one JSON file each under
  `~/Library/Application Support/Diamond Loopz/SourceGlo Pro/Presets/User`,
  appear in the browser under USER, and survive new instances. SAVE
  overwrites the current user preset; on a factory preset it becomes
  Save As.
- **Modified star** — the name shows `*` the moment any covered parameter
  drifts from the loaded preset, computed by comparing live values against a
  snapshot taken at load (never parameter listeners, whose delivery order
  falsely dirtied every EQGlo preset).
- **Undo works** — preset loads run through the UndoManager; an accidental
  switch is one undo away, values and selection both.
- **Presets cover the creative parameters only**: source type, fix amount
  and the seven macros. Gain staging (trims), routing (phase/mono), engine
  quality (HQ/oversampling) and bypass deliberately stay put - switching
  presets never jumps your levels or latency.
- Session state now stores the preset **name**; a restored session shows the
  right preset, clean.

## Verified (make test: 581 checks, 0 failed · auval SUCCEEDED)

Bank size and opening preset; every factory value inside its parameter's
range (the EQGlo-style contract sweep); loads apply and leave excluded
parameters untouched; modified flag flips on a tweak and clears on reload;
undo returns both the previous preset and its values; prev/next wrap both
directions; user presets save, become current, reload in a fresh instance
and round-trip their values; the preset name survives host state save/load.
Preset tests run against a sandboxed user-preset folder.

## Still ahead

Fit / Rescue / Detail tab content, notarisation, Windows build.
