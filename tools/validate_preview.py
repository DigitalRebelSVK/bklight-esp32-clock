#!/usr/bin/env python3
"""Checks the firmware's PNG + frame output and renders LED-style previews.

  python3 tools/validate_preview.py build_host preview

For every raw_*.png written by the preview tool:
  * decodes it with zlib + Pillow and compares pixels with the framebuffer dump
  * rebuilds the BK-Light frame exactly like Bk-Light-AppBypass build_frame()
    (with its 0x65 marker) and compares byte for byte
Then draws upscaled "LED dot" previews and a contact sheet.
"""
import binascii
import glob
import os
import sys
import zlib

from PIL import Image, ImageDraw


def reference_build_frame(png_bytes: bytes) -> bytes:
    # Same layout as bk_light/display_session.py build_frame()
    data_length = len(png_bytes)
    frame = bytearray()
    frame += (data_length + 15).to_bytes(2, "little")
    frame.append(0x02)
    frame += b"\x00\x00"
    frame += data_length.to_bytes(2, "little")
    frame += b"\x00\x00"
    frame += binascii.crc32(png_bytes).to_bytes(4, "little")
    frame += b"\x00\x65"
    frame += png_bytes
    return bytes(frame)


def check_png_chunks(data: bytes) -> None:
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        crc = int.from_bytes(data[pos + 8 + length:pos + 12 + length], "big")
        assert binascii.crc32(ctype + body) == crc, f"bad CRC in {ctype}"
        if ctype == b"IDAT":
            zlib.decompress(body)  # raises on a broken stream / adler32
        pos += 12 + length


def led_preview(img: Image.Image, cell: int = 14) -> Image.Image:
    w, h = img.size
    pad = 10
    out = Image.new("RGB", (w * cell + 2 * pad, h * cell + 2 * pad), (12, 12, 14))
    d = ImageDraw.Draw(out)
    px = img.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            x0 = pad + x * cell + 2
            y0 = pad + y * cell + 2
            if (r, g, b) == (0, 0, 0):
                d.ellipse([x0, y0, x0 + cell - 4, y0 + cell - 4], fill=(28, 28, 32))
            else:
                d.ellipse([x0, y0, x0 + cell - 4, y0 + cell - 4], fill=(r, g, b))
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    names = sorted(os.path.basename(p)[4:-4] for p in glob.glob(os.path.join(src, "raw_*.png")))
    assert names, "no previews found"
    sheets = {}
    for name in names:
        png_bytes = open(os.path.join(src, f"raw_{name}.png"), "rb").read()
        frame = open(os.path.join(src, f"frame_{name}.bin"), "rb").read()
        rgb = open(os.path.join(src, f"rgb_{name}.bin"), "rb").read()
        check_png_chunks(png_bytes)
        img = Image.open(os.path.join(src, f"raw_{name}.png"))
        img.load()
        assert img.mode == "RGB", img.mode
        assert img.tobytes() == rgb, f"{name}: decoded pixels differ from framebuffer"
        assert frame == reference_build_frame(png_bytes), f"{name}: frame differs from reference"
        prev = led_preview(img)
        prev.save(os.path.join(dst, f"{name}.png"))
        sheets.setdefault(name.split("_")[0], []).append((name, prev))
    for size, items in sheets.items():
        label_h = 22
        width = max(p.width for _, p in items)
        height = sum(p.height + label_h for _, p in items)
        sheet = Image.new("RGB", (width, height), (30, 30, 34))
        d = ImageDraw.Draw(sheet)
        y = 0
        for name, p in items:
            d.text((10, y + 5), name.split("_", 1)[1].replace("_", " "), fill=(200, 200, 200))
            sheet.paste(p, (0, y + label_h))
            y += p.height + label_h
        sheet.save(os.path.join(dst, f"sheet_{size}.png"))
    print(f"validated {len(names)} PNGs + frames; previews in {dst}")


if __name__ == "__main__":
    main()
