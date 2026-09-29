#!/usr/bin/env python3
"""Compose host-render frames into one labelled contact sheet PNG.

    contact_sheet.py <frames-dir> <output.png> [name-prefix ...]
"""

import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont


def main() -> int:
    frames_dir = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    prefixes = sys.argv[3:]
    frames = [
        p
        for p in sorted(frames_dir.glob("*.pbm"))
        if not prefixes or any(p.stem.startswith(prefix) for prefix in prefixes)
    ]
    if not frames:
        print("no frames")
        return 1
    columns = 4 if len(frames) > 6 else 2
    scale = 1
    tile_w, tile_h = 400 * scale, 300 * scale
    label_h = 22
    gap = 16
    rows = (len(frames) + columns - 1) // columns
    sheet = Image.new(
        "RGB",
        (columns * tile_w + (columns + 1) * gap, rows * (tile_h + label_h) + (rows + 1) * gap),
        (236, 236, 232),
    )
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 14)
    except OSError:
        font = ImageFont.load_default()
    for index, path in enumerate(frames):
        row, col = divmod(index, columns)
        x = gap + col * (tile_w + gap)
        y = gap + row * (tile_h + label_h + gap)
        frame = Image.open(path).convert("RGB")
        if scale != 1:
            frame = frame.resize((tile_w, tile_h), Image.NEAREST)
        sheet.paste(frame, (x, y + label_h))
        draw.rectangle([x - 1, y + label_h - 1, x + tile_w, y + label_h + tile_h], outline=(90, 90, 90))
        draw.text((x, y + 2), path.stem, fill=(30, 30, 30), font=font)
    sheet.save(output)
    print(f"wrote {output} ({len(frames)} frames)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
