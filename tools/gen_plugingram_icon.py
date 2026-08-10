"""Generate Windows/app icons from PlugingramICON.png."""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

TOOLS = Path(__file__).resolve().parent
WORKSPACE = TOOLS.parent
SRC_CANDIDATES = [
    Path.home() / "Downloads" / "PlugingramICON.png",
    WORKSPACE
    / "plugingram"
    / "Telegram"
    / "Resources"
    / "art"
    / "plugingram_icon_source.png",
]
ART = WORKSPACE / "plugingram" / "Telegram" / "Resources" / "art"


def load_source() -> Image.Image:
    for path in SRC_CANDIDATES:
        if path.is_file():
            print("SOURCE:", path)
            return Image.open(path).convert("RGBA")
    raise FileNotFoundError("PlugingramICON.png not found in Downloads or art/")


def make_transparent_square(im: Image.Image) -> Image.Image:
    pixels = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            if r < 20 and g < 20 and b < 20:
                pixels[x, y] = (0, 0, 0, 0)
    bbox = im.getbbox()
    if not bbox:
        raise RuntimeError("Icon image is empty after background removal")
    cropped = im.crop(bbox)
    cw, ch = cropped.size
    side = max(cw, ch)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(cropped, ((side - cw) // 2, (side - ch) // 2), cropped)
    pad = max(1, int(side * 0.04))
    canvas = Image.new("RGBA", (side + 2 * pad, side + 2 * pad), (0, 0, 0, 0))
    canvas.paste(square, (pad, pad), square)
    return canvas


def main() -> int:
    ART.mkdir(parents=True, exist_ok=True)
    base = make_transparent_square(load_source())
    base.save(ART / "plugingram_icon_source.png")

    for s in (16, 32, 48, 64, 128, 256, 512):
        base.resize((s, s), Image.Resampling.LANCZOS).save(ART / f"icon{s}.png")
        base.resize((s * 2, s * 2), Image.Resampling.LANCZOS).save(
            ART / f"icon{s}@2x.png"
        )

    base.resize((1024, 1024), Image.Resampling.LANCZOS).save(
        ART / "icon_round512@2x.png"
    )

    # Also refresh tray/green variants used by Windows UI chrome.
    green = base.resize((256, 256), Image.Resampling.LANCZOS)
    green.save(ART / "icon_green.png")
    green.save(ART / "iconbig_green.png")

    ico_sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    images = [base.resize(sz, Image.Resampling.LANCZOS) for sz in ico_sizes]
    images[0].save(
        ART / "icon256.ico",
        format="ICO",
        sizes=ico_sizes,
        append_images=images[1:],
    )
    print("OK:", ART / "icon256.ico")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
