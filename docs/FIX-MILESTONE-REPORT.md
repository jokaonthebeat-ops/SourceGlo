# SourceGlo Pro — Fix Source Milestone Report

Date: 2026-08-25 · Version 0.9.2 · Follows the analysis-engine milestone

## What the gold button does now

**FIX SOURCE** toggles a correction chain computed from the last analysis,
scaled live by the Fix Amount parameter (50 % default = half correction,
100 % = the full computed fix):

- **Counter-EQ** — five bands matching the analyser (45 Hz shelf, 150 / 700 /
  3500 Hz bells, 10 kHz shelf) push measured deviations back toward the
  source-type target, beyond a ±2 dB deadzone, capped at ±8 dB. Correction,
  not re-design.
- **Headroom trim** — sources over −0.3 dBTP are pulled to a −1 dBTP working
  ceiling.
- **Low-mono repair** — when the analysis finds a wide low end, side content
  below 120 Hz is summed away.
- **DC filter** — 20 Hz high-pass when DC offset was detected.

Every gain moves through a smoother; engaging, releasing or riding Fix
Amount never clicks. The fix state persists with the session.

## The macros are real DSP now

- **Punch / Transients** — envelope-differential transient shaping; Punch
  keys on the low band (the thump), Transients on the full band. Attack-only:
  the tail is untouched.
- **Body** — +6 dB bell at 180 Hz at full.
- **Tone** — ±4.5 dB spectral tilt around the mids (50 = flat).
- **Air** — +6 dB shelf at 12 kHz at full.
- **Saturate** — tanh drive with level-holding makeup, oversampled 2×/4×/8×
  per the Oversampling parameter when HQ is on; drive 0 is bit-exact dry.
  Oversampling latency is reported to the host and tracks the setting.
- **Stereo** — mid/side width, +60 % side at full; the mid is never touched.

**A/B** compares against the raw source (chain bypassed, trims kept), with a
click-free crossfade. **Pre/Post** on the spectrum now switches between real
taps — watch the fix reshape the curve.

Analyze still measures the *source* (pre-chain), so the score qualifies what
you feed the plugin; the fix is judged by ear and by the Post spectrum.

## Verified (make test: 260 checks, 0 failed · auval SUCCEEDED)

Stage-by-stage absolute targets: neutral chain unity ±0.1 dB; Body +6 dB at
180 Hz; Tone ±4.5 dB tilt both directions; Air +6 dB at 15 kHz; Saturate 0
nulls against dry, full drive holds level within 2 dB and puts the 3rd
harmonic above −40 dBc; Stereo widens the side 1.6× with the mid untouched
±0.05; a +8 dB HighMid deviation is countered −7.5 dB at full fix amount and
half that at 50 %; wide 45 Hz content loses > 12 dB of side under the
low-mono fix; burst attacks gain ≥ 1.25× over their tails with tails within
1.15×; reported latency is 0 with oversampling off and > 0 at 8×.

The existing audio-path contract (gain/phase/mono/bypass) is measured with
the chain neutral, since the parameter defaults intentionally colour.

## Notes

- The engaged FIX SOURCE button uses the supplied "down" state art.
- A/B and bypass crossfade against a zero-latency dry path; with
  oversampling active the wet path is a few samples late, so a brief comb is
  audible *during* the 50 ms crossfade only. Host PDC is correct.
- Still ahead: rescue library scanning + preview, preset bank,
  Fit/Rescue/Detail/Library tab content, notarisation, Windows.
