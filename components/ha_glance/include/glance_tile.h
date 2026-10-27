/* A glance screen tile: a rectangle and the strings in it, with the pure
   helpers that build one. Draws nothing. */
#ifndef GLANCE_TILE_H
#define GLANCE_TILE_H

#include <stdint.h>

#include "bigfont.h"

/* The grid the layouts are drawn on; glance_scale_rect scales from it. */
#define DESIGN_W 480
#define DESIGN_H 320

typedef struct { int16_t x, y, w, h; } rect_t;

typedef struct {
    const bigfont_t *font;
    char text[16];
    int16_t x, line_y; /* screen coordinates: pen start and line top */
    uint16_t color;
} glance_text_t;

#define GLANCE_MAX_TEXTS 8

typedef struct {
    rect_t rect;
    uint16_t background;
    int count;
    glance_text_t texts[GLANCE_MAX_TEXTS];
} glance_tile_t;

uint16_t glance_rgb(uint8_t r, uint8_t g, uint8_t b); /* RGB565 */
/* A rectangle on the 480x320 design grid, scaled to a width by height panel. */
rect_t glance_scale_rect(rect_t design, int width, int height);
/* Appends a string to the tile; a ninth is dropped. */
void glance_add_text(glance_tile_t *tile, const bigfont_t *font, const char *text, int x,
                     int line_y, uint16_t color);

#endif
