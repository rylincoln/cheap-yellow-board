#include "glance_tile.h"

#include <stdio.h>

uint16_t glance_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* value * num / den, rounded to the nearest integer (value, num >= 0). */
static int scale(int value, int num, int den)
{
    return (int)(((long)value * num * 2 + den) / (den * 2));
}

rect_t glance_scale_rect(rect_t design, int width, int height)
{
    rect_t r;
    r.x = (int16_t)scale(design.x, width, DESIGN_W);
    r.y = (int16_t)scale(design.y, height, DESIGN_H);
    r.w = (int16_t)scale(design.w, width, DESIGN_W);
    r.h = (int16_t)scale(design.h, height, DESIGN_H);
    return r;
}

void glance_add_text(glance_tile_t *tile, const bigfont_t *font, const char *text, int x,
                     int line_y, uint16_t color)
{
    if (tile->count >= GLANCE_MAX_TEXTS) {
        return;
    }
    glance_text_t *t = &tile->texts[tile->count++];
    t->font = font;
    snprintf(t->text, sizeof t->text, "%s", text);
    t->x = (int16_t)x;
    t->line_y = (int16_t)line_y;
    t->color = color;
}
