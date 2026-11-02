"""Makes the README animation and the social preview image from the frames of the demo's `--capture`.

1. Record the demo as PNG frames (needs a window; see docs/release.md, "Media"):
       godot --path . res://demo/main.tscn -- --side blue --port 3391 --seconds 50 --capture <frames> ...
2. python tools/make_media.py <frames> [--start 2 --end 40 --speed 2.5] [--width 800 --fps 10] [--preview-at 20]
   writes docs/images/demo.gif (README animation, ~1 MB) and docs/images/social_preview.jpg
   (1280 x 640, for the repository settings).

Needs Pillow (pip install Pillow); it is a maintainer tool, the addon does not use Python.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
CAPTURE_FPS = 10.0  # demo_main.gd CAPTURE_FPS
TITLE = "godot-dis"
SUBTITLE = "DIS (IEEE 1278.1) for Godot 4: entities, fire, exercise control, radio, recording"


def frame(frames: Path, seconds: float) -> Image.Image:
    return Image.open(frames / f"f{round(seconds * CAPTURE_FPS):08d}.png").convert("RGB")


def font(size: int) -> ImageFont.ImageFont:
    for name in ["DejaVuSans.ttf", "segoeui.ttf", "arial.ttf"]:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def make_gif(frames: Path, start: float, end: float, speed: float, width: int, fps: int, colors: int,
             out: Path) -> None:
    images = []
    t = start
    while t < end:
        im = frame(frames, t)
        images.append(im.resize((width, round(im.height * width / im.width)), Image.LANCZOS))
        t += speed / fps
    # One palette for all frames (from a few samples) keeps colours steady and the file smaller.
    sample = Image.new("RGB", (width, images[0].height * 4))
    for i in range(4):
        sample.paste(images[i * len(images) // 4], (0, i * images[0].height))
    palette = sample.quantize(colors=colors, method=Image.Quantize.MEDIANCUT)
    paletted = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in images]
    out.parent.mkdir(parents=True, exist_ok=True)
    paletted[0].save(out, save_all=True, append_images=paletted[1:], duration=round(1000 / fps), loop=0,
                     optimize=True, disposal=1)
    print(f"{out.relative_to(ROOT).as_posix()}: {len(paletted)} frames, {len(paletted) / fps:.1f} s "
          f"({speed:g}x speed), {out.stat().st_size / 1e6:.1f} MB")


def make_preview(frames: Path, at: float, out: Path) -> None:
    im = frame(frames, at)
    im = im.resize((1280, round(im.height * 1280 / im.width)), Image.LANCZOS)
    im = im.crop((0, im.height - 640, 1280, im.height)) if im.height > 640 else im
    draw = ImageDraw.Draw(im, "RGBA")
    draw.rectangle((0, 0, 1280, 120), fill=(16, 20, 28, 240))
    draw.text((40, 18), TITLE, font=font(52), fill=(255, 255, 255))
    draw.text((42, 80), SUBTITLE, font=font(24), fill=(200, 210, 225))
    im.save(out, quality=90, optimize=True)
    print(f"{out.relative_to(ROOT).as_posix()}: {im.width} x {im.height}, {out.stat().st_size / 1e3:.0f} kB")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("frames", type=Path, help="folder with the PNG frames of --capture (f00000000.png, ...)")
    parser.add_argument("--start", type=float, default=2.0, help="first second of the animation")
    parser.add_argument("--end", type=float, default=40.0, help="last second of the animation")
    parser.add_argument("--speed", type=float, default=2.5, help="playback speed (the units move slowly)")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--fps", type=int, default=10)
    parser.add_argument("--colors", type=int, default=64)
    parser.add_argument("--preview-at", type=float, default=20.0, help="second of the social preview frame")
    args = parser.parse_args()
    make_gif(args.frames, args.start, args.end, args.speed, args.width, args.fps, args.colors,
             ROOT / "docs" / "images" / "demo.gif")
    make_preview(args.frames, args.preview_at, ROOT / "docs" / "images" / "social_preview.jpg")


if __name__ == "__main__":
    main()
