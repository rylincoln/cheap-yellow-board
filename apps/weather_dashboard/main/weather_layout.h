/* Weather screen layout: where every string goes, in which font and color.
   Pure C; it draws nothing. */
#ifndef WEATHER_LAYOUT_H
#define WEATHER_LAYOUT_H

#include <stdint.h>

#include "glance_tile.h"
#include "readings.h"

typedef enum { WX_TEMP, WX_HIGH, WX_LOW, WX_RAIN_TODAY, WX_RAIN_WEEK, WX_SHED_PCT, WX_SHED_DAWN, WX_COUNT } wx_sensor_t;

typedef enum { TILE_OUTSIDE, TILE_RAIN_TODAY, TILE_RAIN_WEEK, TILE_SHED, WEATHER_TILE_COUNT } weather_tile_t;

/* The request buffer for WX_COUNT entity IDs of 95 characters. */
#define WX_BODY_SIZE 1024

/* Zeroes tiles first, so whole tiles compare with memcmp. Only the
   temperature and the shed charge go gray when old. */
void weather_compose(int width, int height, const reading_t readings[WX_COUNT], int64_t now_us,
                     glance_tile_t tiles[WEATHER_TILE_COUNT]);

#endif
