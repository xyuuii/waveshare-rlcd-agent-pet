#!/usr/bin/env python3
"""Copy host-rendered screens into the bridge dashboard as small 2-colour PNGs.

    python3 tools/host_render/export_ui_previews.py <host-render-out> <bridge-ui-dir>

The images come from the real firmware drawing code (see build.sh), so the
dashboard shows exactly the pixels the board draws. They are still only a
preview: contrast, viewing angle and response time of the reflective panel
differ from a monitor.
"""

import pathlib
import sys

from PIL import Image

PAPER = (214, 217, 207)
INK = (35, 38, 42)

PICKS = {
    "clock-sans": "clock-sans",
    "clock-segment": "clock-segment",
    "clock-dots": "clock-dots",
    "clock-analog": "clock-analog",
    "clock-words": "clock-words",
    "clock-terminal": "clock-terminal",
    "clock-pet": "clock-pet",
    "page-overview": "overview-codex-thinking",
    "page-usage": "usage-codex",
    "page-clock": "clock-segment-attention-12h",
    "egg-builtin": "egg-builtin-2600ms",
}


def export(src: pathlib.Path, dst: pathlib.Path) -> None:
    gray = Image.open(src).convert("L")
    # Palette index 0 = ink (dark pixels), 1 = paper.
    table = bytes(0 if value < 128 else 1 for value in range(256))
    out = Image.frombytes("P", gray.size, gray.tobytes().translate(table))
    out.putpalette(list(INK) + list(PAPER) + [0, 0, 0] * 254)
    out.save(dst, optimize=True, bits=1)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src_dir = pathlib.Path(sys.argv[1])
    ui_dir = pathlib.Path(sys.argv[2]) / "previews"
    ui_dir.mkdir(parents=True, exist_ok=True)
    missing = 0
    for name, scene in PICKS.items():
        src = src_dir / f"{scene}.pbm"
        if not src.exists():
            print(f"missing {src}", file=sys.stderr)
            missing += 1
            continue
        export(src, ui_dir / f"{name}.png")
        print(f"wrote {ui_dir / name}.png")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
