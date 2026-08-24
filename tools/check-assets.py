#!/usr/bin/env python3
"""Validates every PNG under Assets/ at the file-format level.

This exists because macOS and Windows do not agree about what a valid PNG is.
JUCE decodes PNGs through ImageIO on macOS and through bundled libpng on
Windows. ImageIO accepts a file whose IDAT stream is truncated - it returns the
rows it managed to read and says nothing. libpng refuses it:

    libpng error: IDAT: CRC error

and the editor goes down with it. EQGlo shipped from its first commit with
Assets/controls/knobs/eqglo-knob-128px-128f-vertical.png truncated at 108 of
its 128 frames. It came that way inside the template zip - the zip's own CRC
was fine, so nothing upstream noticed either. Every macOS build, every auval
run and every screenshot was clean; the first Windows CI run crashed in
pluginval's Editor test.

So this check must not go through an image decoder, or it inherits whichever
platform's leniency it happens to run on. It reads the container directly:

  * every chunk's CRC matches the bytes it covers
  * the file ends with IEND
  * the concatenated IDAT stream inflates
  * the inflated stream holds exactly the number of scanlines IHDR declares

The last one is the one that matters here. A truncated file can still have
valid CRCs on the chunks that survived.

Exits non-zero and names every offending file, so it works as a build step.
"""

import pathlib
import struct
import sys
import zlib

# Bytes-per-pixel numerator by PNG colour type: grey, -, RGB, palette, grey+A,
# -, RGBA.
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def problems_with(path: pathlib.Path):
    data = path.read_bytes()

    if data[:8] != SIGNATURE:
        return ["not a PNG"]

    if len(data) < 26:
        return ["shorter than a PNG header"]

    width, height = struct.unpack(">II", data[16:24])
    depth, colour = data[24], data[25]

    found = []
    compressed = b""
    saw_iend = False
    offset = 8

    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        body = data[offset + 8 : offset + 8 + length]
        stored = data[offset + 8 + length : offset + 12 + length]

        name = kind.decode("ascii", errors="replace")

        if len(body) < length or len(stored) < 4:
            found.append(f"{name} truncated")
            break

        if struct.unpack(">I", stored)[0] != (zlib.crc32(kind + body) & 0xFFFFFFFF):
            found.append(f"{name} CRC mismatch")

        if kind == b"IDAT":
            compressed += body

        if kind == b"IEND":
            saw_iend = True
            break

        offset += 12 + length

    if not saw_iend:
        found.append("no IEND")

    try:
        raw = zlib.decompress(compressed)
    except zlib.error as error:
        found.append(f"IDAT will not inflate: {error}")
        return found

    stride = (width * CHANNELS.get(colour, 4) * depth + 7) // 8 + 1
    rows = len(raw) // stride

    if rows != height:
        found.append(f"{rows} scanlines present, IHDR declares {height}")

    return found


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "Assets")

    if not root.is_dir():
        print(f"check-assets: {root} is not a directory")
        return 2

    files = sorted(root.rglob("*.png"))

    if not files:
        print(f"check-assets: no PNGs under {root} - is the path right?")
        return 2

    bad = 0

    for path in files:
        issues = problems_with(path)

        if issues:
            bad += 1
            print(f"  BAD  {path}")
            for issue in issues:
                print(f"       {issue}")

    if bad:
        print(f"\ncheck-assets: {bad} of {len(files)} PNGs are malformed")
        return 1

    print(f"check-assets: {len(files)} PNGs valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
