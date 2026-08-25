# SourceGlo Pro — Analysis Engine Milestone Report

Date: 2026-08-25 · Version 0.9.1 · Follows the approved UI milestone

## What the engine does now

Press **ANALYZE** while the source plays and SourceGlo Pro measures the last
12 seconds of audio (a lock-free rolling capture; the audio thread never
blocks) on a worker thread, then fills the whole interface with its verdict:

- **Source Score 0–100** with the READY / GOOD / NEEDS WORK / FIX REQUIRED
  phrase, from a weighted blend of the five pods.
- **Pods** — Tone (spectral balance vs the source-type profile), Punch (crest
  window + onset contrast), Level (peak window + true-peak ceiling), Phase
  (mono compatibility, low end weighted hardest), Fit (how the source sits
  against a modern mix-target curve).
- **Source stats** — Peak / RMS / Crest / True Peak run live from the meters
  (BS.1770-style 4× true peak, ported from MasterGlo Pro); Duration, Tempo
  (onset autocorrelation, 60–180 BPM) and Key (high-resolution chroma +
  Krumhansl–Schmuckler, bass-weighted) fill in per analysis. Unknowns show
  "–" instead of guessing.
- **Diagnostics** — rule-driven cards: clipping, headroom, low-end masking
  (with the red conflict overlay on the spectrum), weak body, mud, harshness,
  phase cancellation, wide low end, flattened/uncontrolled transients, DC
  offset — plus positive cards (Clipping Clean, Phase Coherent, Level In
  Range, Tone On Target) so a healthy source reads as healthy. Top four by
  severity.
- **Masking/Fit** — radar of measured band energy vs the mix target, fit
  score and the five band bars, all from the analysis.
- **Spectrum** — the dashed reference is now the real target curve for the
  selected source type; the idle fake trace is gone (the display sinks to the
  floor when audio stops).
- **Source-type profiles** for all 12 types (Auto → Other), tuned for the
  hip hop / R&B catalogue: band-balance targets, peak/crest windows,
  low-end and width expectations.

Fresh instances start in an honest empty state — "––" score, PRESS ANALYZE
pill, no diagnostics — instead of demo data. Fix Source re-analyses for now;
its correction DSP is the next milestone.

## Verified (make test: 236 checks, 0 failed)

Ground-truth fixtures pin the maths down with absolute targets:

- 1 kHz sine at −6.02 dBFS → peak/RMS/crest/true-peak within 0.15–0.35 dB.
- Full-scale fs/4 sine at 45° (every sample on ±0.7071) → **0 dBTP** ±0.35.
- 128 / 92 BPM kick patterns → tempo within ±2 BPM.
- C-minor and A-major triads → "C Minor" / "A Major" exactly.
- Clipped vs clean signals → correct clipping diagnostics.
- Dual-mono vs polarity-flipped noise → phase ≥ 90 vs ≤ 20 + phase diagnostic.
- Sub-heavy source: no false conflict as Kick, real conflict as Loop.
- Determinism, not-enough-audio gating, full processor publish path.

## Two bugs the tests caught before any ear did

1. **The silence gate mono-summed** — perfectly out-of-phase stereo cancelled
   to digital silence and was "not enough audio", so the one signal the phase
   diagnostics exist for skipped analysis entirely. The gate now uses
   per-channel energy. (The first phase test "passed" against the empty
   result — the assertion that would have to break was added alongside the
   fix.)
2. **Chroma quantisation** — the shared 4096-point analysis grid has ~11.7 Hz
   bins, coarser than a semitone below ~200 Hz, so a 110 Hz tone landed on
   G#/A# and "A major" detected as C# minor. Key detection now runs its own
   32768-point FFT (1.5 Hz bins).

## Still ahead

Fix Source correction DSP + macro processing, rescue library scanning and
preview, preset bank, Fit/Rescue/Detail/Library tab content, notarisation,
Windows build.
