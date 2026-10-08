#!/usr/bin/env python3
"""Generate the PS1-native indexed menu background used by the interactive-menu build."""

from __future__ import annotations

import json
import struct
import zlib
from pathlib import Path


WIDTH = 240
HEIGHT = 240
ASSET_ID = "9ef42b9c5a7d4862a9d1336e1f9c72a4"

# The authored camera clear color is (30, 17, 41). The lines approximate the
# PS1 half-blend result of the authored grid and scanline colors over that clear.
# Both 12-pixel vertical and 16-pixel horizontal grid periods, plus the 3-pixel
# scanline cadence, repeat cleanly across this 240x240 tile.
PALETTE = [
    (24, 16, 40),   # camera clear
    (120, 104, 144),  # scanline
    (72, 48, 100),  # grid
    (96, 76, 120),  # scanline over grid
] + [(24, 16, 40)] * 12


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def make_indexed_png() -> bytes:
    rows = bytearray()
    for y in range(HEIGHT):
        packed_row = bytearray()
        for x in range(0, WIDTH, 2):
            values = []
            for pixel_x in (x, x + 1):
                on_scanline = y % 3 == 1
                on_grid = pixel_x % 12 == 0 or y % 16 == 0
                if on_scanline and on_grid:
                    value = 3
                elif on_scanline:
                    value = 1
                elif on_grid:
                    value = 2
                else:
                    value = 0
                values.append(value)
            packed_row.append((values[0] << 4) | values[1])
        rows.append(0)  # PNG filter: None
        rows.extend(packed_row)

    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 4, 3, 0, 0, 0)
    palette = bytes(channel for color in PALETTE for channel in color)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"PLTE", palette)
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9))
        + png_chunk(b"IEND", b"")
    )


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    asset = root / "assets" / "textures" / "ps1" / "main_menu_background.png"
    asset.parent.mkdir(parents=True, exist_ok=True)
    asset.write_bytes(make_indexed_png())
    asset.with_suffix(asset.suffix + ".hmeta").write_text(
        json.dumps({"version": 1, "assetId": ASSET_ID, "formerAssetIds": []}, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"{asset}: {WIDTH}x{HEIGHT}, indexed 4-bit, {WIDTH * HEIGHT // 2} texel bytes")


if __name__ == "__main__":
    main()
