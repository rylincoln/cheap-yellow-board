/* Glance screen layout: where every string goes, in which font and color.
   Pure C; it draws nothing. */
#ifndef GLANCE_LAYOUT_H
#define GLANCE_LAYOUT_H

#include <stdint.h>

#include "bigfont.h"
#include "readings.h"

typedef struct { int16_t x, y, w, h; } rect_t;

typedef struct {
    const bigfont_t *font;
    char text[16];
    int16_t x, line_y; /* screen coordinates: pen start and line top */
    uint16_t color;
} glance_text_t;

typedef struct {
    rect_t rect;
    uint16_t background;
    int count;
    glance_text_t texts[4];
} glance_tile_t;

/* A tile for each sensor up to the battery, whose tile also shows the
   battery's charge: tiles[s] is sensor s's tile. */
#define GLANCE_TILE_COUNT (SENSOR_BATTERY + 1)

uint16_t glance_rgb(uint8_t r, uint8_t g, uint8_t b); /* RGB565 */
/* Zeroes tiles first, so whole tiles compare with memcmp. show_charge
   adds the battery's charge to its tile. */
void glance_compose(int width, int height, const reading_t readings[SENSOR_COUNT], bool show_charge,
                    int64_t now_us, glance_tile_t tiles[GLANCE_TILE_COUNT]);

#endif
