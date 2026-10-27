/* Home Assistant readings: parse the template reply, age and format them. */
#ifndef READINGS_H
#define READINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define READING_STALE_US (300LL * 1000 * 1000)

typedef struct {
    bool available;         /* the last reply had a number for it */
    double value;
    int64_t reported_at_us; /* board time the sensor last reported to HA */
} reading_t;

typedef enum { BATTERY_IDLE, BATTERY_CHARGING, BATTERY_DRAINING, BATTERY_UNKNOWN } battery_direction_t;

/* The JSON body for POST /api/template asking for count entities, leaving
   out empty entity IDs; length, or -1 if size is too small. */
int readings_template_body(const char *const entity_ids[], int count, char *out, size_t size);
/* Sets every reading from one reply; returns how many are available. An
   entity with an empty ID stays unavailable. */
int readings_apply(const char *reply, const char *const entity_ids[], int count,
                   int64_t now_us, reading_t readings[]);
bool reading_stale(const reading_t *reading, int64_t now_us);
/* "66°" (UTF-8 degree sign), or "--" when unavailable or 999.5 or more either way. */
void format_temperature(const reading_t *reading, char *out, size_t size);
/* Magnitude only: "3.9", "153", "0.0", or "--" when unavailable or 999.5 A or more. */
battery_direction_t format_battery(const reading_t *reading, char *out, size_t size);
/* "82", without the sign, and returns that percent; "--" and -1 when
   unavailable or outside -0.5 to 100.5. */
int format_percent(const reading_t *reading, char *out, size_t size);
/* Inches of rain in four characters: "0.12", "9.99", "10.0", "99.9". Returns 1
   for rain, 0 for a dry "0.00" (never "-0.00"), and -1 with "--" when
   unavailable or outside -0.005 to 99.95. */
int format_rain(const reading_t *reading, char *out, size_t size);

#endif
