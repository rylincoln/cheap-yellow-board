/* Draws the glance screen's tiles on the panel. */
#ifndef GLANCE_H
#define GLANCE_H

#include <stdbool.h>

#include "glance_layout.h"

/* Draws each tile in strips of up to 16 rows. With full, the screen is
   cleared to black first and every tile drawn; without, a tile identical to
   the one last drawn in its place is skipped. */
void glance_draw(const glance_tile_t tiles[GLANCE_TILE_COUNT], bool full);

#endif
