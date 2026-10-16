#!/usr/bin/env python3
"""Samples the centre of each of the 15 bands of tests/reference/falsecolor_bands.png and compares
it with the palette in falsecolor_core.cpp (max 8/255 per channel). Skips (exit 77) if the PNG or
Pillow is missing."""
import os, re, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
png = os.path.join(root, "tests", "reference", "falsecolor_bands.png")
if not os.path.exists(png):
    print("SKIP: tests/reference/falsecolor_bands.png not provided"); sys.exit(77)
try:
    from PIL import Image
except ImportError:
    print("SKIP: Pillow not installed (pip install pillow)"); sys.exit(77)
src = open(os.path.join(root, "falsecolor_core.cpp"), encoding="utf-8").read()
block = src[src.index("kPalette255"):]
block = block[:block.index("};")]
pal = [tuple(map(int, m)) for m in re.findall(r"\{(\d+),\s*(\d+),\s*(\d+)\}", block)]
assert len(pal) == 15, "palette must have 15 entries"
im = Image.open(png).convert("RGB")
W, H = im.size
bad = 0
for k in range(15):
    x = int((k + 0.5) * W / 15); y = H // 2
    px = im.getpixel((x, y))
    d = max(abs(a - b) for a, b in zip(px, pal[k]))
    if d > 8:
        bad += 1; print(f"FAIL band {k}: image {px} palette {pal[k]} diff {d}")
print("OK" if not bad else f"FAILED ({bad})"); sys.exit(1 if bad else 0)
