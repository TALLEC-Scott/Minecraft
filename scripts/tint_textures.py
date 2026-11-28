#!/usr/bin/env python3
"""Bake a foliage colour into a grayscale texture (Minecraft-style multiply tint).

The game has no runtime biome tint, so Pixel Perfection leaves and plants,
which are grayscale and meant to be tinted, get their colour baked in at import.

    uv run --no-project --with pillow python scripts/tint_textures.py SRC DST RRGGBB

Colours used for the shipped textures (see assets/Textures/README.md):
    spruce leaves 619961, birch leaves 80A755 (Minecraft's fixed foliage colours),
    fern / large fern 6ADE3C (maps Mineclonia's tall grass onto the old home-made tall_grass.png),
    tall grass / double tall grass 8BDB46 (the average green of grass.png's top).
"""
import sys

from PIL import Image


def tint(src: str, dst: str, hex_color: str) -> None:
    tr, tg, tb = (int(hex_color[i : i + 2], 16) for i in (0, 2, 4))
    im = Image.open(src).convert("RGBA")
    out = Image.new("RGBA", im.size)
    out.putdata([(r * tr // 255, g * tg // 255, b * tb // 255, a) for r, g, b, a in im.getdata()])
    out.save(dst)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    tint(sys.argv[1], sys.argv[2], sys.argv[3].lstrip("#"))
