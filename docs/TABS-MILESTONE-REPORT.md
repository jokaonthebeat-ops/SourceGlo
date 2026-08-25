# SourceGlo Pro — Tab Views Milestone Report

Date: 2026-08-25 · Version 0.9.5 · Follows the preset-bank milestone

## No more placeholders

All five lower tabs now carry real views:

- **ANALYZE** — spectrum + masking/fit (since the engine milestone).
- **FIT** — the band-balance story at full width: a ±12 dB deviation chart
  of the five bands against the source-type target, colour-coded (within
  ±2 dB green, to ±5 dB gold, beyond red), each bar labelled with its exact
  offset; alongside it the fit score, wide band-fit bars, and a plain-English
  callout of the biggest offset — noting whether Fix Source is already
  countering it.
- **RESCUE** — the full library browser: twelve ranked matches with
  favourites and fit %, a detail pane with a large waveform overview,
  PREVIEW/STOP button, favourite star, double-click to audition, and rows
  drag out into the DAW as files.
- **DETAIL** — the complete readout: duration/tempo/key, both correlation
  figures, fix state, the band table (level vs target vs offset,
  colour-coded), and the score breakdown showing each pod's value and weight
  summing to the Source Score.
- **LIBRARY** — folder management (since the library milestone — and now
  actually visible: see the bug below).

Every view has an honest empty state before the first analysis.

## A layout bug this milestone flushed out

The LIBRARY tab's folder manager had been invisible since it shipped: the
patch that was supposed to give it bounds inside the tab component never
matched the file (the target text had drifted) and failed silently, so the
view existed with zero size. The new tabs hit the same wall immediately,
which is how it surfaced. All five content views now share one bounds line.

## Verified (make test: 591 checks, 0 failed · auval SUCCEEDED)

New model plumbing is pinned: after an analysis the model carries the full
band picture (levels referenced to the loudest band, deviations, both
correlations) that the Fit and Detail tabs render.

## Remaining

Notarisation (needs the Apple credentials stored once), Windows build
(needs a GitHub remote for the Actions runner).
