# SourceGlo Pro — marketing assets

Everything here is generated from the real plugin. The screenshots and every
frame of the film are the actual editor rendering actual analysis: real audio
goes through the real chain, and the scores, diagnostics, spectrum and rescue
rows on screen are measurements, not mockups.

## What's here

| File | What it is |
| --- | --- |
| `SALES-PAGE.md` | Long-form copy: description, features, captions |
| `STORE-LISTINGS.txt` | Store form fields, plain text, no markdown |
| `screenshots/*.png` | Full-resolution masters (1491x1055) |
| `screenshots/*-web.jpg` | Quality-82 JPEGs for web use |
| `SourceGloPro-demo.mp4` | 116 s demo film, 1080p, 14 Mbps delivery master |
| `SourceGloPro-demo-web.mp4` | The same film at 5 Mbps for uploads |
| `video-stills/` | One frame per act, for checking a render without scrubbing |

## The film

`make video` renders it; `make video-web` renders the upload copy.

Thirteen acts, 116 seconds:

| At | Act | What it shows |
| --- | --- | --- |
| 0:00 | Logo open | Premium mark, wordmark, tagline |
| 0:05 | Know before you build | The panel arrives |
| 0:11 | Analyze | Source type, then a real Analyze press |
| 0:20 | Source Score | The five pods |
| 0:28 | Diagnostics | The cards naming real problems |
| 0:37 | Fix Source | Engaged live, Fix Amount ridden 20 → 100 % |
| 0:49 | Eight macros | Each one sweeps in turn, Sub first |
| 1:01 | Fit | Band balance vs target |
| 1:10 | Detail | Full readout and score breakdown |
| 1:19 | Rescue | The library browser |
| 1:30 | Library | Folder management |
| 1:37 | Presets | Five factory presets loading live |
| 1:48 | Logo close | Formats, Diamond Loopz |

### Sound

By default the film is **silent**: a generated drum bed drives the analysis so
everything on screen is real, but a synthetic loop presented as a soundtrack
would cheapen the product.

Pass a real loop and the film gains a soundtrack — and it is the plugin's own
**processed output**, so the viewer hears what SourceGlo Pro did to the audio
they are watching it analyse:

```
make video ARGS="/path/to/loop.wav"
```

Optional third and fourth arguments are the start offset in seconds and the
bitrate in Mbps: `ARGS="loop.wav 8 14"`.

### Rescue rows in the film

A headless render has no user library, so the tool generates a small demo pack
in a scratch folder and points a sandboxed index at it. Customer instances show
their own samples.
