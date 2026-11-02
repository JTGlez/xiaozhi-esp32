#!/usr/bin/env python3
"""Rebuild Elena's board GIFs from the avatar kit, without its right-side crop.

The kit crops each panel of its master sheet to 320 source pixels, but the art
reaches about 360: the thought bubble, the music note and other marks lose
their right side. Its recolor and mouth repair depend on fixed positions, so
this script keeps the kit's final PNG frames untouched, restores the missing
strip from the master sheet and moves the result 16 px left to fit the canvas.

It also sets each GIF's frames and timing for this board. `listening` is a
single frame because the microphone is being encoded while it shows.

Requires Pillow and the kit, which is not tracked:

    python3 scripts/elena_avatar_refit.py --kit <kit folder>
"""

from __future__ import annotations

import argparse
import colorsys
import hashlib
import json
import sys
from pathlib import Path

from PIL import Image, ImageSequence

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import elena_avatar_assets as assets  # noqa: E402

CANVAS = (320, 240)
BACKGROUND = (249, 240, 233)
COLORS = 64

# The kit's geometry: panel origins in the master sheet, and how it fits them.
PANEL_X = (27, 409, 783, 1163)
PANEL_Y = (107, 416, 718)
SCALE = 0.875
FIT_X, FIT_Y = 20, 30
FIT_HEIGHT = 210
KIT_CROP = 320
SHIFT = 16

# The art's reds are a dusty burgundy that looks washed out on this panel.
RED_GAIN = 1.9
RED_DARKEN = 0.10

# Longest a GIF frame can last, in ms: a static image is redrawn once per delay.
STATIC_MS = 655350

# name: (panel row, panel column, kit frame numbers, duration of each in ms)
GIFS = {
    "neutral": (0, 0, (0, 1), (600, 600)),
    "happy": (0, 1, (0, 1, 2, 3), (160, 120, 160, 280)),
    "look_right": (0, 2, (0, 1, 2, 3), (160, 120, 500, 160)),
    "thinking": (0, 3, (0, 1, 2, 3), (220, 220, 220, 420)),
    "speaking": (1, 0, (0, 1, 2, 3, 4), (130, 130, 130, 130, 130)),
    "listening": (1, 1, (0,), (STATIC_MS,)),
    "email": (1, 2, (0, 1, 2, 3, 4), (240, 200, 200, 200, 600)),
    "shake": (1, 3, (0, 1, 2, 3), (110, 110, 110, 110)),
    "sleeping": (2, 0, (0, 3), (630, 630)),
    "sad": (2, 1, (0, 1, 2, 3), (280, 280, 240, 420)),
    "look_left": (2, 2, (0, 1, 2, 3), (160, 120, 500, 160)),
    "pet": (2, 3, (0, 1, 2, 3), (180, 140, 180, 300)),
}


def panel_width(sheet: Image.Image, x: int, y: int) -> int:
    """Source width up to, and not including, the panel's right border."""
    for width in range(KIT_CROP, 400):
        column = [sheet.getpixel((min(x + width, sheet.width - 1), y + t)) for t in range(30, 180, 5)]
        if sum(1 for pixel in column if min(pixel) < 150) >= len(column) * 0.8:
            return width - 3
    raise ValueError(f"No panel border to the right of x={x}")


def restored_strip(sheet: Image.Image, row: int, column: int) -> Image.Image:
    """The part of a panel that the kit cropped, fitted like the rest of the art."""
    x, y = PANEL_X[column], PANEL_Y[row]
    width = panel_width(sheet, x, y)
    fitted = round(width * SCALE)
    panel = sheet.crop((x, y, x + width, y + KIT_CROP * 3 // 4))
    panel = panel.resize((fitted, FIT_HEIGHT), Image.Resampling.LANCZOS)
    strip = panel.crop((round(KIT_CROP * SCALE), 0, fitted, FIT_HEIGHT))
    # The sheet's panel color differs slightly from the canvas: this avoids a seam.
    pixels = strip.load()
    for j in range(strip.height):
        for i in range(strip.width):
            if sum(abs(a - b) for a, b in zip(pixels[i, j], BACKGROUND)) < 14:
                pixels[i, j] = BACKGROUND
    return strip


def strengthen_reds(image: Image.Image) -> None:
    """Saturate reds, fading out towards the light tones that are skin and blush."""
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            hue, light, saturation = colorsys.rgb_to_hls(*(v / 255 for v in pixels[x, y]))
            degrees = hue * 360
            if (degrees <= 14 or degrees >= 335) and 0.12 < light < 0.62 and saturation > 0.14:
                weight = min(1, (0.62 - light) / 0.14)
                rgb = colorsys.hls_to_rgb(hue, light * (1 - RED_DARKEN * weight),
                                          min(1, saturation * (1 + (RED_GAIN - 1) * weight)))
                pixels[x, y] = tuple(round(v * 255) for v in rgb)


def refit(frame: Image.Image, strip: Image.Image) -> Image.Image:
    out = Image.new("RGB", CANVAS, BACKGROUND)
    out.paste(frame.convert("RGB").crop((SHIFT, 0, CANVAS[0], CANVAS[1])), (0, 0))
    out.paste(strip, (FIT_X + round(KIT_CROP * SCALE) - SHIFT, FIT_Y))
    # Leftovers of the panel's rounded corners, in margins no expression uses.
    out.paste(BACKGROUND, (0, FIT_Y, 14, FIT_Y + 16))
    out.paste(BACKGROUND, (296, FIT_Y, CANVAS[0], FIT_Y + 16))
    strengthen_reds(out)
    return out


def write_gif(frames: list[Image.Image], durations: tuple[int, ...], path: Path) -> None:
    # One palette for the whole GIF, from all its frames: mouth and marks keep their colors.
    mosaic = Image.new("RGB", (CANVAS[0] * len(frames), CANVAS[1]))
    for index, frame in enumerate(frames):
        mosaic.paste(frame, (index * CANVAS[0], 0))
    palette = mosaic.quantize(colors=COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    images = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    images[0].save(path, save_all=True, append_images=images[1:], duration=list(durations), loop=0,
                   optimize=False, disposal=2)


def write_frame_oracle(gifs_dir: Path, output: Path) -> None:
    """Hashes of Pillow's composed frames, an independent check of the firmware decoder."""
    version = Image.__version__
    gifs = {}
    for name in sorted(GIFS):
        with Image.open(gifs_dir / f"{name}.gif") as gif:
            gifs[name] = [
                {"sha256_bgra": hashlib.sha256(frame.convert("RGBA").tobytes("raw", "BGRA")).hexdigest()}
                for frame in ImageSequence.Iterator(gif)
            ]
    output.write_text(json.dumps({
        "oracle": f"Pillow {version}: RGBA to BGRA, full composed frames; elena_avatar_refit.py output",
        "gifs": gifs,
    }, indent=1) + "\n", encoding="utf-8")


def update_sums(asset_dir: Path) -> None:
    sums = asset_dir / "SHA256SUMS"
    names = [line.split(maxsplit=1)[1] for line in sums.read_text(encoding="utf-8").splitlines() if line.strip()]
    lines = [f"{hashlib.sha256((asset_dir / name).read_bytes()).hexdigest()}  {name}" for name in names]
    sums.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", required=True, type=Path, help="Avatar kit folder")
    args = parser.parse_args()

    sheet = Image.open(args.kit / "source" / "elena_12_expressions.png").convert("RGB")
    asset_dir = assets.DEFAULT_ASSET_DIR
    gifs_dir = asset_dir / "gifs"
    for name, (row, column, numbers, durations) in GIFS.items():
        strip = restored_strip(sheet, row, column)
        frames = [refit(Image.open(args.kit / "frames" / f"{name}_{number:02d}.png"), strip) for number in numbers]
        write_gif(frames, durations, gifs_dir / f"{name}.gif")

    assets.prepare_avatar_fallback(gifs_dir / "neutral.gif", asset_dir / assets.FALLBACK_FILE)
    update_sums(asset_dir)
    write_frame_oracle(gifs_dir, assets.GIF_FIXTURES / "v3-frames.json")

    total = 0
    for name in sorted(GIFS):
        metadata = assets.gif_metadata(gifs_dir / f"{name}.gif")
        size = (gifs_dir / f"{name}.gif").stat().st_size
        total += size
        print(f"{name:11s} {metadata['frames']} frames {size:7d} B")
    print(f"total {total} B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
