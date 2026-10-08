#!/usr/bin/env python3
"""Turn the screen test's PBM images into PNGs that look like the panel.

  render_png.py IN_DIR OUT_DIR [--scale 3] [--names a,b,...] [--sheet sheet.png --columns 4]

IN_DIR holds <name>.pbm files: tests/display/screens/golden, or the
screens/ folder of a build of that test. Each PNG is the 144 x 168 image
scaled by --scale in the memory LCD's colours (the design's grey-green
panel and near-black pixels). --sheet also lays them out in one image, each
labelled with its name.
"""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

PANEL = (185, 188, 179)  # #B9BCB3, the design's LCD background
INK = (22, 24, 26)       # #16181A
PAGE = (236, 237, 234)   # #ECEDEA


def load(path):
    img = Image.open(path)
    if img.size != (144, 168):
        raise SystemExit(f"{path}: expected 144 x 168, got {img.size}")
    return img.convert("1")


def colour(img, scale):
    rgb = Image.new("RGB", img.size, PANEL)
    rgb.paste(Image.new("RGB", img.size, INK), mask=img.point(lambda p: 255 if p == 0 else 0).convert("1"))
    return rgb.resize((img.width * scale, img.height * scale), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("in_dir", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--names", help="comma-separated names, in order (default: all, sorted)")
    ap.add_argument("--sheet", type=Path, help="also write every image on one sheet")
    ap.add_argument("--columns", type=int, default=4)
    args = ap.parse_args()

    names = args.names.split(",") if args.names else sorted(p.stem for p in args.in_dir.glob("*.pbm"))
    args.out_dir.mkdir(parents=True, exist_ok=True)
    images = []
    for name in names:
        img = colour(load(args.in_dir / f"{name}.pbm"), args.scale)
        img.save(args.out_dir / f"{name}.png")
        images.append((name, img))

    if args.sheet and images:
        w, h = images[0][1].size
        pad, label = 24, 28
        cols = min(args.columns, len(images))
        rows = (len(images) + cols - 1) // cols
        sheet = Image.new("RGB", (pad + cols * (w + pad), pad + rows * (h + label + pad)), PAGE)
        draw = ImageDraw.Draw(sheet)
        font = ImageFont.load_default(size=16)
        for i, (name, img) in enumerate(images):
            x = pad + (i % cols) * (w + pad)
            y = pad + (i // cols) * (h + label + pad)
            sheet.paste(img, (x, y))
            draw.text((x, y + h + 6), name.replace("_", " "), fill=INK, font=font)
        sheet.save(args.sheet)


if __name__ == "__main__":
    main()
