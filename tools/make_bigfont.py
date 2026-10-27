#!/usr/bin/env python3
"""Rasterises DejaVu Sans Bold into the big fonts of the home and weather dashboards.

Run: uv run --no-project --with pillow==13.0.0 --with matplotlib==3.11.2 python tools/make_bigfont.py
Writes bigfont_data.c and bigfont_data.h in apps/home_dashboard/main and
apps/weather_dashboard/main. The pinned versions keep the output byte for byte
the same.
"""
import math
import os

import matplotlib
from matplotlib import font_manager
from PIL import Image, ImageDraw, ImageFont

APPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "apps")

# The outside tile's room for its number on each board, in pixels: the
# tile's width less TILE_PAD in glance_layout.c (256 - 16, and 171 - 16).
OUTSIDE_ROOM = (240, 155)
# The weather Outside tile is 288 and 192 wide, less the same 16.
WEATHER_ROOM = (272, 176)
# A fitted font is the largest em below its bound's at which these fit the
# room, so a reading this wide stays larger than the font named as its floor.
FIT_TEXTS = ("-10°", "100°")
LABEL_CHARS = " " + "ABCDEFGHIJKLMNOPQRSTUVWXYZ" + "abcdefghijklmnopqrstuvwxyz" + "0123456789" + "-%"
NUM_CHARS = " 0123456789.-°A"
# The battery strip's icons, drawn rather than taken from the font: the
# draining arrow, the charging bolt, and a battery filled 0 to 10 tenths at
# U+E000 + tenths (private use).
BOLT = "\u26a1"
BATTERY = 0xE000
SUNRISE = 0xE010
BATTERIES = "".join(chr(BATTERY + tenths) for tenths in range(11))
STRIP_CHARS = NUM_CHARS + "▼" + BOLT + BATTERIES
DIGITS = " 0123456789"
UPPER = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

# Per app: its output folder and its fonts as (name, (480 board, 320 board)
# ems, characters, fit). fit is None or (room, bound, floor): the em is
# fitted, and bound and floor name the fonts it must stay below and above.
APPS_FONTS = (
    (os.path.join(APPS, "home_dashboard", "main"), (
        ("label", (20, 14), LABEL_CHARS, None),
        ("strip", (48, 28), STRIP_CHARS, None),
        ("tile", (72, 48), NUM_CHARS, None),
        ("outside", (120, 80), NUM_CHARS, None),
        ("outside_mid", (None, None), NUM_CHARS, (OUTSIDE_ROOM, "outside", "tile")),
    )),
    (os.path.join(APPS, "weather_dashboard", "main"), (
        ("label", (20, 14), LABEL_CHARS, None),
        ("small", (14, 10), UPPER, None),
        ("hilo", (32, 21), DIGITS + "-°▲▼", None),
        ("rain", (48, 28), DIGITS + ".-", None),
        ("shed", (40, 24), DIGITS + "-→" + chr(SUNRISE) + BATTERIES, None),
        ("temp", (120, 80), DIGITS + "-°", None),
        ("temp_mid", (None, None), DIGITS + "-°", (WEATHER_ROOM, "temp", "hilo")),
    )),
)
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
    """The largest em under largest at which every FIT_TEXTS fits room."""
    for em in range(largest - 1, 0, -1):
        font = ImageFont.truetype(path, em)
        if all(text_width(font, text) <= room for text in FIT_TEXTS):
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


def sunrise(w, h):
    """A half sun on a horizon bar, with rays above it."""
    bar = max(2, round(h * 0.12))
    cy = h - bar
    r = min((cy) / 1.8, w / 2 / 1.75)
    big = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    cx = w * SS / 2
    d.rectangle([0, cy * SS, w * SS - 1, h * SS - 1], fill=255)
    d.pieslice([cx - r * SS, (cy - r) * SS, cx + r * SS, (cy + r) * SS], 180, 360, fill=255)
    for deg in (160, 125, 90, 55, 20):
        c, s = math.cos(math.radians(deg)), -math.sin(math.radians(deg))
        d.line([(cx + c * r * 1.3 * SS, (cy + s * r * 1.3) * SS),
                (cx + c * r * 1.75 * SS, (cy + s * r * 1.75) * SS)], fill=255, width=max(2, round(bar * 0.8)) * SS)
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
        if ch in "▲▼" + BOLT or BATTERY <= code <= BATTERY + 10 or code == SUNRISE:
            # Icons stand on the baseline as tall as a digit.
            h = zero[3] - zero[1]
            if ch == "▼":
                w = round(0.9 * zero_adv)
                img = polygon([(0, 0), (1, 0), (0.5, 1)], w, h)
            elif ch == "▲":
                w = round(0.9 * zero_adv)
                img = polygon([(0, 1), (1, 1), (0.5, 0)], w, h)
            elif code == SUNRISE:
                w = round(1.8 * h)
                img = sunrise(w, h)
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
    for out, fonts in APPS_FONTS:
        write_app(path, out, fonts, c_header, h_header)


def write_app(path, out, fonts, c_header, h_header):
    sizes = {name: ems for name, ems, _, _ in fonts}
    c = [c_header, '#include "bigfont_data.h"\n']
    h = [h_header, "#ifndef BIGFONT_DATA_H\n#define BIGFONT_DATA_H\n", '#include "bigfont.h"\n']
    for name, ems, chars, fit in fonts:
        for board, suffix in enumerate(("480", "320")):
            em = ems[board]
            if fit:
                room, bound, floor = fit
                em = fit_em(path, room[board], sizes[bound][board])
                assert em > sizes[floor][board]
            ident = "%s_%s" % (name, suffix)
            ascent, line_height, glyphs, bitmap = build(path, em, chars)
            c.append("static const uint8_t %s_bitmap[%d] = {\n%s\n};\n" % (ident, len(bitmap), c_bytes(bitmap)))
            rows = ",\n".join("    {0x%x, %d, %d, %d, %d, %d, %d}" % g for g in glyphs)
            c.append("static const bigfont_glyph_t %s_glyphs[%d] = {\n%s,\n};\n" % (ident, len(glyphs), rows))
            c.append("const bigfont_t bigfont_%s = {%d, %d, %d, %s_glyphs, %s_bitmap};\n"
                     % (ident, line_height, ascent, len(glyphs), ident, ident))
            h.append("extern const bigfont_t bigfont_%s;" % ident)
            total = len(bitmap) + len(glyphs) * 12
            print("%-16s em %3d line %3d: %d glyphs, %d bitmap bytes, ~%d bytes with glyph table"
                  % (ident, em, line_height, len(glyphs), len(bitmap), total))
    h.append("\n#endif\n")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "bigfont_data.c"), "w", newline="\n") as f:
        f.write("\n".join(c))
    with open(os.path.join(out, "bigfont_data.h"), "w", newline="\n") as f:
        f.write("\n".join(h))


if __name__ == "__main__":
    main()
