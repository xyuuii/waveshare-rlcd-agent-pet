#!/usr/bin/env python3
"""Convert host-render PBM dumps to PNG (1x and 2x) for quick review.

The 2x images use nearest-neighbour scaling so every panel pixel stays a
crisp square; they are previews of the drawing code, not of the physical
reflective panel (contrast, viewing angle and response time differ).
"""

import pathlib
import sys

from PIL import Image


def main() -> int:
    out_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "host-render-out")
    count = 0
    for pbm in sorted(out_dir.glob("*.pbm")):
        image = Image.open(pbm).convert("L")
        image.save(pbm.with_suffix(".png"))
        image.resize((image.width * 2, image.height * 2), Image.NEAREST).save(
            pbm.with_name(pbm.stem + "@2x.png")
        )
        count += 1
    print(f"converted {count} frame(s) in {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
