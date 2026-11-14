#!/usr/bin/env python3
"""Generate the Switch homebrew icon from the launcher's banner artwork.

Usage: python tools/make_switch_icon.py
Writes betrayal/port/switch/icon.jpg (256x256 JPEG, the size hbmenu expects).
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "betrayal" / "port" / "src" / "launcher" / "assets" / "banner_source.png"
OUT = ROOT / "betrayal" / "port" / "switch" / "icon.jpg"


def main() -> None:
    image = Image.open(SOURCE).convert("RGB")
    width, height = image.size
    side = min(width, height)
    left = (width - side) // 2
    top = (height - side) // 2
    image = image.crop((left, top, left + side, top + side)).resize((256, 256), Image.LANCZOS)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    image.save(OUT, quality=92)
    print(f"wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
