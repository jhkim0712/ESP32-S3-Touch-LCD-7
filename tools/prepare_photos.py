#!/usr/bin/env python3
"""Convert a photo folder for the ESP32-S3 photo frame.

The device decodes JPEGs with the ESP32-S3 ROM TJpgDec, which only supports
baseline, 8-bit, 3-channel JPEGs. Photos saved from the web/SNS are often
progressive, and full-size phone photos (3-8 MB) take seconds to read over SPI.

This script walks SRC recursively and writes to DST (same folder structure):
  - applies EXIF orientation, then drops EXIF
  - fits each photo inside --size (default 800x800, works for any screen rotation)
  - saves a baseline JPEG, 4:2:0, quality --quality
  - HEIC/HEIF (iPhone) is converted too if `pillow-heif` is installed

Usage:
    pip install pillow            # optional: pip install pillow-heif
    python tools/prepare_photos.py  D:/MyPhotos  E:/photos
Then copy DST to the SD card as /photos (or point DST directly at the card).
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageOps

try:
    from pillow_heif import register_heif_opener

    register_heif_opener()
    HEIF = True
except ImportError:
    HEIF = False

EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp", ".gif", ".tif", ".tiff"}
if HEIF:
    EXTS |= {".heic", ".heif"}


def convert(src: Path, dst: Path, size: tuple[int, int], quality: int) -> bool:
    try:
        with Image.open(src) as im:
            im = ImageOps.exif_transpose(im)
            if im.mode != "RGB":
                im = im.convert("RGB")
            im.thumbnail(size, Image.Resampling.LANCZOS)
            dst.parent.mkdir(parents=True, exist_ok=True)
            im.save(dst, "JPEG", quality=quality, progressive=False, subsampling="4:2:0", optimize=True)
        return True
    except Exception as e:  # noqa: BLE001 - report and continue with the next file
        print(f"  skip {src}: {e}", file=sys.stderr)
        return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path, help="source folder (searched recursively)")
    ap.add_argument("dst", type=Path, help="output folder, e.g. <SD card>/photos")
    ap.add_argument("--size", default="800x800", help="max WxH box (default 800x800)")
    ap.add_argument("--quality", type=int, default=88, help="JPEG quality 1-95 (default 88)")
    args = ap.parse_args()

    w, h = (int(v) for v in args.size.lower().split("x"))
    files = [p for p in args.src.rglob("*") if p.suffix.lower() in EXTS and p.is_file()]
    if not HEIF and any(p.suffix.lower() in {".heic", ".heif"} for p in args.src.rglob("*")):
        print("note: HEIC files found - install pillow-heif to convert them", file=sys.stderr)

    ok = 0
    for i, src in enumerate(sorted(files), 1):
        dst = (args.dst / src.relative_to(args.src)).with_suffix(".jpg")
        print(f"[{i}/{len(files)}] {src.relative_to(args.src)}")
        ok += convert(src, dst, (w, h), args.quality)
    print(f"done: {ok}/{len(files)} converted into {args.dst}")
    return 0 if ok == len(files) else 1


if __name__ == "__main__":
    sys.exit(main())
