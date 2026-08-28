VST3 and standalone, 64-bit, built and validated on a Windows runner with
pluginval at strictness 5 — the same gate the macOS build clears with `auval`.

**Download the ZIP below.** It contains two ways to install, and `INSTALL.txt`
explains both.

**Manual install is recommended.** Copy `SourceGlo Pro.vst3` into
`C:\Program Files\Common Files\VST3\` and rescan plugins in your DAW. Copying a
folder never triggers SmartScreen, so there is no security warning at all.

**The installer is not code-signed**, so Windows will show "Windows protected
your PC". Choose *More info* → *Run anyway*. That warning is about the missing
signature, not about anything found in the file.

For the standalone, keep `SourceGlo Pro.exe` and the `Assets` folder together —
the app reads its interface artwork from that folder and opens with plain grey
controls without it.
