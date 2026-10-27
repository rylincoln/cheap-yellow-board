/* Glance screen layout: where every string goes, in which font and color.
   Pure C; it draws nothing. */
#ifndef GLANCE_LAYOUT_H
#define GLANCE_LAYOUT_H

#include <stdint.h>

#include "glance_tile.h"
#include "readings.h"
#include "sensors.h"

/* A tile for each sensor up to the battery, whose tile also shows the
   battery's charge: tiles[s] is sensor s's tile. */
#define GLANCE_TILE_COUNT (SENSOR_BATTERY + 1)

/* Zeroes tiles first, so whole tiles compare with memcmp. show_charge
   adds the battery's charge to its tile. */
void glance_compose(int width, int height, const reading_t readings[SENSOR_COUNT], bool show_charge,
                    int64_t now_us, glance_tile_t tiles[GLANCE_TILE_COUNT]);

#endif
