# SourceGlo Pro — Rescue Library Milestone Report

Date: 2026-08-25 · Version 0.9.3 · Follows the Fix Source milestone

## The rescue rows are real now

- **Library scanning** — add your sample folders in the LIBRARY tab (or via
  Browse Library); a low-priority background thread indexes every
  wav/aif/flac/mp3/m4a: band balance, peak, crest, duration and a 24-bar
  waveform overview per file. The index persists as JSON under
  `~/Library/Application Support/Diamond Loopz/SourceGlo Pro/` and rescans
  touch only new or changed files (mtime-gated). Local files only — nothing
  leaves the machine.
- **Fit ranking** — suggestions are the library files that best match what
  the selected source type is *supposed* to be. Scoring is asymmetric per
  band: in a profile's prominent bands, missing and excess energy both hurt;
  in its background bands only excess hurts (a kick isn't penalised for
  having quiet highs). Ties break deterministically.
- **Rows** — real file names, real waveform thumbnails, descriptors derived
  from each file's own analysis (Deep/Warm/Full/Bright/Crisp ·
  Punchy/Dense/Tight/Extended/Solid), live fit %.
- **Preview** — the play button auditions the file through the plugin output
  (lock-free, 5 ms fades, auto-stops at the end, −6 dB).
- **Drag out** — drag a row straight into the DAW as a real file drop.
- **Favourites** — stars persist in the index and nudge ranking (+4).
- **Auto Match** — on: the list refreshes after every analysis and scan;
  off: it holds until you refresh manually.
- **LIBRARY tab** — folder management, scan progress, index count.

## Verified (make test: 296 checks, 0 failed · auval SUCCEEDED)

Generated wav fixtures with known spectra pin the pipeline down: per-file
analysis (a 55 Hz kick reads sub-dominant, a hat reads high-dominant,
durations exact), full scan finds all fixtures, index persists and reloads,
ranking puts low-end one-shots first and the hat last for a Kick — and the
hat first for the Hat type — deterministically; favourites survive a reload;
the preview is audible over silent input and stops to silence. All library
tests run against a sandboxed index path, never the user's real one.

## A bug the fixtures caught

The first scoring formula (symmetric mean deviation) saturated on sparse
one-shots — a pure 808 sine has −60 dB mids, so every candidate bottomed out
at the same clamped score and the "ranking" degenerated to alphabetical
order, hat first. The asymmetric penalty above is the fix, and the test that
caught it now pins the ranking.

## Notes

- The screenshot's five suggestions come from a *generated demo library* in
  a scratch folder (the headless shot has no user samples); real instances
  show your own packs.
- Still ahead: preset bank, Fit/Rescue/Detail tab content, notarisation,
  Windows build.
