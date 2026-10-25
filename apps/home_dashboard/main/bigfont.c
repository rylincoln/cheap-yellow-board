#include "bigfont.h"

#include <stddef.h>

#define BAD_CODE 0xFFFFFFFFu

/* Decodes one UTF-8 sequence of 1 to 3 bytes and advances *s past it. A
   malformed or truncated sequence consumes one byte and yields BAD_CODE;
   this never reads past the terminating NUL. */
static uint32_t next_code(const unsigned char **s)
{
    const unsigned char *p = *s;
    uint32_t c = p[0];
    if (c < 0x80) {
        *s = p + 1;
        return c;
    }
    if (c >= 0xC2 && c < 0xE0 && (p[1] & 0xC0) == 0x80) {
        *s = p + 2;
        return (c & 0x1F) << 6 | (p[1] & 0x3F);
    }
    if (c >= 0xE0 && c < 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *s = p + 3;
        return (c & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
    }
    *s = p + 1;
    return BAD_CODE;
}

static const bigfont_glyph_t *find_glyph(const bigfont_t *font, uint32_t code)
{
    int lo = 0, hi = (int)font->glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = font->glyphs[mid].code;
        if (c == code)
            return &font->glyphs[mid];
        if (c < code)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

int bigfont_text_width(const bigfont_t *font, const char *text)
{
    const unsigned char *s = (const unsigned char *)text;
    int width = 0;
    while (*s) {
        const bigfont_glyph_t *g = find_glyph(font, next_code(&s));
        if (g)
            width += g->advance;
    }
    return width;
}

static uint16_t blend(uint16_t bg, uint16_t fg, int alpha)
{
    int r = bg >> 11, g = (bg >> 5) & 63, b = bg & 31;
    int fr = fg >> 11, fg_ = (fg >> 5) & 63, fb = fg & 31;
    r += (fr - r) * alpha / 15;
    g += (fg_ - g) * alpha / 15;
    b += (fb - b) * alpha / 15;
    return (uint16_t)(r << 11 | g << 5 | b);
}

void bigfont_render(const bigfont_t *font, const char *text, int x, int line_y,
                    uint16_t color, uint16_t *pixels, int width, int first_row, int rows)
{
    const unsigned char *s = (const unsigned char *)text;
    int pen = x;
    while (*s) {
        const bigfont_glyph_t *g = find_glyph(font, next_code(&s));
        if (!g)
            continue;
        int stride = (g->width + 1) / 2;
        int left = pen + g->x_offset;
        for (int gy = 0; gy < g->height; gy++) {
            int row = line_y + g->y_offset + gy - first_row;
            if (row < 0 || row >= rows)
                continue;
            const uint8_t *src = font->bitmap + g->offset + (size_t)gy * stride;
            uint16_t *dst = pixels + (size_t)row * width;
            for (int gx = 0; gx < g->width; gx++) {
                int col = left + gx;
                if (col < 0 || col >= width)
                    continue;
                int alpha = (gx & 1) ? (src[gx / 2] & 0x0F) : (src[gx / 2] >> 4);
                if (alpha)
                    dst[col] = blend(dst[col], color, alpha);
            }
        }
        pen += g->advance;
    }
}
