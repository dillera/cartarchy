#!/usr/bin/env python3
"""Draw art/logo.png: the app icon's globe and swords over a CARTARCHY
wordmark, in the Amiga client's 16 pens (src/gfx.c's palette); and
art/icon.png, the emblem in Workbench 1.3's four pens for mkicon.py.

Drawn with square pixels, then stretched to hires, where a pixel is half as
wide as it is tall. Needs Pillow and macOS's Copperplate font, so the PNG is
committed and builds only run png2planes.py on it:

    uv run --with pillow support/mklogo.py ../artwork/cartarchy-icon-master-1024.png art
"""
import sys

from PIL import Image, ImageDraw, ImageFont

PALETTE = [0x136, 0x024, 0x000, 0xFFF, 0xDCA, 0xE33, 0x3B3, 0xEC2, 0xA5D, 0x2CC, 0xF82,
           0xCCD, 0x889, 0x445, 0xFFF, 0x9BF]
INK, WHITE, YELLOW, ORANGE, RED, LIGHT, FACE, DARK, SKY = 2, 3, 7, 10, 5, 11, 12, 13, 15

FONT = "/System/Library/Fonts/Supplemental/Copperplate.ttc"
W, H = 152, 56          # square pixels; the PNG is 2W x H
EMBLEM = 34


def emblem(path, height, pens, ink=0.3):
    """The icon, cropped to its art and stretched for hires. Each pixel takes
    the pen of what covers most of it (outline, blades, background, globe),
    except that outline wins from a share of ink, so thin lines survive."""
    src = Image.open(path).convert("L")
    box = src.point(lambda v: 255 if v < 200 else 0).getbbox()
    src = src.crop(box)
    size = (height * 2, height)
    classes = [(0, 90), (90, 200), (200, 238), (238, 256)]
    cover = [src.point(lambda v, a=a, b=b: 255 if a <= v < b else 0).resize(size, Image.BOX)
             for a, b in classes]
    out = Image.new("P", size, pens[2])
    for y in range(height):
        for x in range(size[0]):
            shares = [c.getpixel((x, y)) / 255 for c in cover]
            best = 0 if shares[0] >= ink else max(range(1, 4), key=lambda i: shares[i])
            out.putpixel((x, y), pens[best])
    return out


def wordmark():
    """CARTARCHY as a mask, to fill in bands, outline and shadow."""
    font = ImageFont.truetype(FONT, 21, index=2)
    text = "CARTARCHY"
    l, t, r, b = font.getbbox(text)
    mask = Image.new("L", (r - l + 4, b - t + 4), 0)
    ImageDraw.Draw(mask).text((2 - l, 2 - t), text, font=font, fill=255)
    return mask


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    # Hires canvas: 2 pixels across per square pixel
    out = Image.new("P", (W * 2, H), FACE)
    out.putpalette(sum(([(c >> 8) * 17, (c >> 4 & 15) * 17, (c & 15) * 17] for c in PALETTE), []))
    # The blades in brass, the globe a little blue
    em = emblem(sys.argv[1], EMBLEM, (INK, YELLOW, FACE, SKY))
    out.paste(em, ((W * 2 - em.width) // 2, 0))

    mask = wordmark()
    wide = mask.resize((mask.width * 2, mask.height), Image.NEAREST)
    x0, y0 = (W * 2 - wide.width) // 2, EMBLEM + 1
    on = lambda x, y: 0 <= x < wide.width and 0 <= y < wide.height and wide.getpixel((x, y)) > 110
    px = out.load()
    for y in range(wide.height + 2):
        for x in range(wide.width + 4):
            X, Y = x0 + x, y0 + y
            if not (0 <= X < W * 2 and 0 <= Y < H):
                continue
            if on(x, y):
                band = y / wide.height
                px[X, Y] = YELLOW if band < 0.45 else ORANGE if band < 0.8 else RED
            elif any(on(x + dx, y + dy) for dx in (-2, -1, 1, 2) for dy in (-1, 0, 1)):
                px[X, Y] = INK
            elif on(x - 3, y - 1) or on(x - 4, y - 2):
                px[X, Y] = DARK
    out.save(f"{sys.argv[2]}/logo.png")

    # Workbench 1.3: blue, white, black, orange
    icon = emblem(sys.argv[1], 26, (2, 3, 0, 1))
    icon.putpalette([0x00, 0x55, 0xAA, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x22, 0xFF, 0x88, 0x00])
    icon.save(f"{sys.argv[2]}/icon.png")


if __name__ == "__main__":
    main()
