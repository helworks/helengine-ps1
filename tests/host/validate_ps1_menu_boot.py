"""Validate the runtime TTY log from the rotating PS1 menu build."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def validate(text: str) -> list[str]:
    errors: list[str] = []
    if "PS1_AUTHORED_FIRST_FRAME" not in text:
        errors.append("the authored menu did not reach its first frame")
    if "PS1_UI_FONT_CROSSES_PAGE" in text or "Abort at " in text:
        errors.append("the runtime aborted while preparing the menu UI")

    font_upload = re.search(
        r"PS1_TEXTURE_UPLOADED w=256 h=256 bpp=4 vram=(\d+),(\d+)", text
    )
    if font_upload is None:
        errors.append("the 256x256 indexed font atlas was not uploaded")
    elif int(font_upload.group(2)) % 256 + 256 > 256:
        errors.append("the font atlas crosses a vertical PS1 texture-page boundary")

    # The PS1 cook contract caps this source at 64x64 Indexed8. The exporter report describes
    # the 240x240 source image, not the cooked runtime texture dimensions. Four tiles reference
    # that texture; the runtime currently uploads each tile's texture instance independently.
    tile_uploads = re.findall(
        r"PS1_TEXTURE_UPLOADED w=64 h=64 bpp=8 vram=(\d+),(\d+)", text
    )
    if len(tile_uploads) < 4:
        errors.append("the four PS1-cooked menu background tiles were not uploaded")
    for x_text, y_text in tile_uploads:
        x, y = int(x_text), int(y_text)
        if x % 64 + 16 > 64 or y % 256 + 64 > 256:
            errors.append("a menu tile texture crosses a PS1 texture-page boundary")
            break

    if not re.search(r"PS1_AUTHORED_UI .*failure=none", text):
        errors.append("the authored UI did not complete a draw without a failure marker")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tty_log", type=Path, help="DuckStation log containing PS1 TTY output")
    args = parser.parse_args()
    text = args.tty_log.read_text(encoding="utf-8", errors="replace")
    errors = validate(text)
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("PASS: rotating PS1 menu booted, uploaded page-safe font/tile textures, and drew its UI.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
