/* Draws the glance screen's tiles on the panel. */
#ifndef GLANCE_DRAW_H
#define GLANCE_DRAW_H

#include <stdbool.h>

#include "glance_tile.h"

#define GLANCE_MAX_TILES 8

/* Draws each of count tiles, up to GLANCE_MAX_TILES, in strips of up to 16
   rows. With full, the screen is cleared to black first and every tile
   drawn; without, a tile identical to the one last drawn in its place is
   skipped. */
void glance_draw(const glance_tile_t *tiles, int count, bool full);

#endif
