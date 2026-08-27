#!/bin/bash
# Renders the demo film's narration with macOS TTS.
#
# Run this from YOUR terminal session, not from an automated one: the speech
# service refuses to synthesise to a file from some non-interactive contexts
# (it writes a 4096-byte header and no audio), and AVSpeechSynthesizer's
# offline callback never fires there either.
#
#   ./tools/make-narration.sh [voice]
#
# Then: make video BEAT=... and the film mixes these in automatically,
# ducking the beat under each line.

set -e
VOICE="${1:-Samantha}"
DIR="$(cd "$(dirname "$0")/.." && pwd)/build/vo"
mkdir -p "$DIR"

say_line () {   # $1 = index, $2 = text
  local out="$DIR/act-$1.aiff"
  local wav="$DIR/act-$1.wav"
  rm -f "$out" "$wav"
  say -v "$VOICE" -r 180 -o "$out" "$2"
  # say writes AIFF most reliably; convert to the 48 kHz mono WAV the film reads
  afconvert -f WAVE -d LEI16@48000 -c 1 "$out" "$wav" >/dev/null 2>&1 || true
  rm -f "$out"
  if [ -s "$wav" ]; then
    printf "  act-%-2s %s\n" "$1" "$(afinfo "$wav" | awk -F': ' '/estimated duration/{printf "%.1fs", $2}')"
  else
    printf "  act-%-2s FAILED\n" "$1"
  fi
}

echo "Rendering narration with voice: $VOICE"
say_line 1  "Your mix is only as good as what you feed it."
say_line 2  "Pick the source type, play it, and press Analyze."
say_line 3  "Tone, punch, level, phase and fit. Five scores, one number."
say_line 4  "It names the problem in plain language."
say_line 5  "One button applies the correction the analysis computed."
say_line 6  "Eight macros: sub, punch, body, tone, air, stereo, transients and saturate."
say_line 7  "Band balance against the target for that exact source."
say_line 8  "Every measurement, and how the score is built."
say_line 9  "When it cannot be saved, ranked matches from your own library."
say_line 10 "Point it at your packs once. It works in every session."
say_line 11 "Twenty nine factory presets by source type, and your own."
echo "Done. Now run: make video BEAT=/path/to/beat.wav"
