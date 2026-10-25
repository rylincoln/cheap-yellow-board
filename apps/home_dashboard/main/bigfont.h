/* Anti-aliased big fonts: 4-bit alpha glyph bitmaps blended into RGB565
   strips. Pure C, no hardware access. */
#ifndef BIGFONT_H
#define BIGFONT_H

#include <stdint.h>

typedef struct {
    uint32_t code;     /* Unicode code point */
    uint8_t width, height;
    int8_t x_offset;   /* ink box left, from the pen position */
    uint8_t y_offset;  /* ink box top, from the line top */
    uint8_t advance;
    uint32_t offset;   /* into bitmap: 4-bit alpha, high nibble first, rows padded to a byte */
} bigfont_glyph_t;

typedef struct {
    uint8_t line_height, ascent;
    uint16_t glyph_count;
    const bigfont_glyph_t *glyphs; /* sorted by code */
    const uint8_t *bitmap;
} bigfont_t;

/* Pixels; unknown code points are 0 wide. UTF-8 input. */
int bigfont_text_width(const bigfont_t *font, const char *text);
/* Blends text into a width x rows row-major RGB565 buffer. x is the pen
   start in buffer columns; line_y (the line top) and first_row (the
   buffer's top row) share one row coordinate. Clips at the buffer's edges. */
void bigfont_render(const bigfont_t *font, const char *text, int x, int line_y,
                    uint16_t color, uint16_t *pixels, int width, int first_row, int rows);

#endif
