#!/usr/bin/env python3
"""Rasterises DejaVu Sans Bold into the home dashboard's big fonts.

Run: uv run --no-project --with pillow==13.0.0 --with matplotlib==3.11.2 python tools/make_bigfont.py
Writes apps/home_dashboard/main/bigfont_data.c and bigfont_data.h. The pinned
versions keep the output byte for byte the same.
"""
import os

import matplotlib
from matplotlib import font_manager
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "apps", "home_dashboard", "main")

SIZES = {  # name: (480 board, 320 board), the em in pixels; None is fitted
    "label": (20, 14),
    "strip": (48, 28),
    "tile": (72, 48),
    "outside": (120, 80),
    "outside_mid": (None, None),
}
# The outside tile's room for its number on each board, in pixels: the
# tile's width less TILE_PAD in glance_layout.c (256 - 16, and 171 - 16).
OUTSIDE_ROOM = (240, 155)
# outside_mid is the largest em at which these fit OUTSIDE_ROOM, so an
# outside reading this wide stays larger than the inside tiles.
OUTSIDE_MID_TEXTS = ("-10°", "100°")
LABEL_CHARS = " " + "ABCDEFGHIJKLMNOPQRSTUVWXYZ" + "abcdefghijklmnopqrstuvwxyz" + "0123456789" + "-%"
NUM_CHARS = " 0123456789.-°A"
# The battery strip's icons, drawn rather than taken from the font: the
# draining arrow, the charging bolt, and a battery filled 0 to 10 tenths at
# U+E000 + tenths (private use).
BOLT = "\u26a1"
BATTERY = 0xE000
STRIP_CHARS = NUM_CHARS + "▼" + BOLT + "".join(chr(BATTERY + tenths) for tenths in range(11))
SS = 4  # supersampling for the icons


def quantise(img):
    return [round(v * 15 / 255) for v in img.tobytes()]


def pack(rows):
    """rows: list of lists of 0..15 -> bytes, high nibble first, rows padded."""
    out = bytearray()
    for r in rows:
        for i in range(0, len(r), 2):
            hi = r[i]
            lo = r[i + 1] if i + 1 < len(r) else 0
            out.append(hi << 4 | lo)
    return bytes(out)


def text_width(font, text):
    """The width bigfont_text_width gives: the sum of the rounded advances."""
    return sum(round(font.getlength(ch)) for ch in text)


def fit_em(path, room, largest):
    """The largest em under largest at which every OUTSIDE_MID_TEXTS fits room."""
    for em in range(largest - 1, 0, -1):
        font = ImageFont.truetype(path, em)
        if all(text_width(font, text) <= room for text in OUTSIDE_MID_TEXTS):
            return em
    raise ValueError("nothing fits %d pixels" % room)


def licence(path):
    """The DejaVu fonts licence, from next to the font or from matplotlib."""
    candidates = [os.path.join(os.path.dirname(path), "LICENSE_DEJAVU"),
                  os.path.join(matplotlib.get_data_path(), "fonts", "ttf", "LICENSE_DEJAVU")]
    for candidate in candidates:
        if os.path.exists(candidate):
            with open(candidate, encoding="utf-8") as f:
                text = f.read()
            assert "*/" not in text and "Bitstream Vera" in text
            return [line.rstrip() for line in text.strip().splitlines()]
    raise FileNotFoundError("LICENSE_DEJAVU is not next to %s" % path)


def polygon(points, w, h):
    """Fills points, given as fractions of w and h."""
    big = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(big).polygon([(x * w * SS, y * h * SS) for x, y in points], fill=255)
    return big.resize((w, h), Image.BOX)


def battery(tenths, w, h):
    """A battery on its side, terminal to the right, filled tenths/10 from the left."""
    stroke = max(2, round(h * 0.11))
    inset = stroke + max(1, round(stroke * 0.75))
    nub_w = max(2, round(w * 0.07))
    nub_h = round(h * 0.4)
    nub_h += (h - nub_h) % 2  # centred on whole pixels
    body = w - nub_w
    big = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    d.rounded_rectangle([0, 0, body * SS - 1, h * SS - 1], radius=round(stroke * 1.5) * SS,
                        outline=255, width=stroke * SS)
    top = (h - nub_h) // 2
    d.rectangle([body * SS, top * SS, w * SS - 1, (top + nub_h) * SS - 1], fill=255)
    fill = round((body - 2 * inset) * SS * tenths / 10)
    if fill > 0:
        d.rectangle([inset * SS, inset * SS, inset * SS + fill - 1, (h - inset) * SS - 1], fill=255)
    return big.resize((w, h), Image.BOX)


def build(path, em, chars):
    font = ImageFont.truetype(path, em)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    assert line_height < 256
    zero = font.getbbox("0", anchor="la")
    zero_adv = font.getlength("0")
    glyphs = []
    bitmap = bytearray()
    pad = em
    for ch in sorted(chars, key=ord):
        code = ord(ch)
        if ch in "▼" + BOLT or BATTERY <= code <= BATTERY + 10:
            # Icons stand on the baseline as tall as a digit.
            h = zero[3] - zero[1]
            if ch == "▼":
                w = round(0.9 * zero_adv)
                img = polygon([(0, 0), (1, 0), (0.5, 1)], w, h)
            elif ch == BOLT:
                w = round(0.75 * h)
                img = polygon([(0.42, 0), (1, 0), (0.7, 0.38), (1, 0.38), (0.22, 1),
                               (0.4, 0.58), (0, 0.58)], w, h)
            else:
                w = round(1.9 * h)
                img = battery(code - BATTERY, w, h)
            x_off, y_off = 0, ascent - h
            adv = w + round((0.2 if code >= BATTERY else 0.1) * em)
        else:
            adv = round(font.getlength(ch))
            canvas = Image.new("L", (adv + 2 * pad, line_height + 2 * pad), 0)
            ImageDraw.Draw(canvas).text((pad, pad), ch, font=font, fill=255, anchor="la")
            box = canvas.getbbox()
            if box is None:
                img, x_off, y_off = None, 0, 0
            else:
                img = canvas.crop(box)
                x_off, y_off = box[0] - pad, box[1] - pad
        if img is None:
            glyphs.append((code, 0, 0, 0, 0, adv, len(bitmap)))
            continue
        w, h = img.size
        assert -128 <= x_off < 128 and 0 <= y_off < 256 and adv < 256 and w < 256 and h < 256
        px = quantise(img)
        rows = [px[r * w:(r + 1) * w] for r in range(h)]
        glyphs.append((code, w, h, x_off, y_off, adv, len(bitmap)))
        bitmap += pack(rows)
    return ascent, line_height, glyphs, bytes(bitmap)


def c_bytes(data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ",".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


def main():
    path = font_manager.findfont(
        font_manager.FontProperties(family="DejaVu Sans", weight="bold"), fallback_to_default=False)
    header = """/* Generated by tools/make_bigfont.py. Do not edit.
 *
 * Regenerate with:
 *   uv run --no-project --with pillow==13.0.0 --with matplotlib==3.11.2 python tools/make_bigfont.py
 *
"""
    c_header = header + """ * Glyphs are rasterised from DejaVu Sans Bold. The glyph bitmaps are derived
 * from that font; the arrow, bolt and battery icons are drawn. The font's licence:
 *
""" + "".join((" * " + line).rstrip() + "\n" for line in licence(path)) + " */\n"
    h_header = header + """ * The fonts' licence is in bigfont_data.c.
 */
"""
    c = [c_header, '#include "bigfont_data.h"\n']
    h = [h_header, "#ifndef BIGFONT_DATA_H\n#define BIGFONT_DATA_H\n", '#include "bigfont.h"\n']
    for name, sizes in SIZES.items():
        chars = LABEL_CHARS if name == "label" else STRIP_CHARS if name == "strip" else NUM_CHARS
        for board, suffix in enumerate(("480", "320")):
            em = sizes[board]
            if em is None:
                em = fit_em(path, OUTSIDE_ROOM[board], SIZES["outside"][board])
                assert em > SIZES["tile"][board]
            ident = "%s_%s" % (name, suffix)
            ascent, line_height, glyphs, bitmap = build(path, em, chars)
            c.append("static const uint8_t %s_bitmap[%d] = {\n%s\n};\n" % (ident, len(bitmap), c_bytes(bitmap)))
            rows = ",\n".join("    {0x%x, %d, %d, %d, %d, %d, %d}" % g for g in glyphs)
            c.append("static const bigfont_glyph_t %s_glyphs[%d] = {\n%s,\n};\n" % (ident, len(glyphs), rows))
            c.append("const bigfont_t bigfont_%s = {%d, %d, %d, %s_glyphs, %s_bitmap};\n"
                     % (ident, line_height, ascent, len(glyphs), ident, ident))
            h.append("extern const bigfont_t bigfont_%s;" % ident)
            total = len(bitmap) + len(glyphs) * 12
            print("%-16s em %3d: %d glyphs, %d bitmap bytes, ~%d bytes with glyph table"
                  % (ident, em, len(glyphs), len(bitmap), total))
    h.append("\n#endif\n")
    with open(os.path.join(OUT, "bigfont_data.c"), "w", newline="\n") as f:
        f.write("\n".join(c))
    with open(os.path.join(OUT, "bigfont_data.h"), "w", newline="\n") as f:
        f.write("\n".join(h))


if __name__ == "__main__":
    main()
