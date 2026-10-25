/* Home Assistant readings: parse the template reply, age and format them. */
#ifndef READINGS_H
#define READINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { SENSOR_UPSTAIRS, SENSOR_DOWNSTAIRS, SENSOR_OUTSIDE, SENSOR_BATTERY, SENSOR_COUNT } sensor_t;
#define READING_STALE_US (300LL * 1000 * 1000)

typedef struct {
    bool available;         /* the last reply had a number for it */
    double value;
    int64_t reported_at_us; /* board time the sensor last reported to HA */
} reading_t;

typedef enum { BATTERY_IDLE, BATTERY_CHARGING, BATTERY_DRAINING, BATTERY_UNKNOWN } battery_direction_t;

/* The JSON body for POST /api/template; length, or -1 if size is too small. */
int readings_template_body(const char *const entity_ids[SENSOR_COUNT], char *out, size_t size);
/* Sets every reading from one reply; returns how many are available. */
int readings_apply(const char *reply, const char *const entity_ids[SENSOR_COUNT],
                   int64_t now_us, reading_t readings[SENSOR_COUNT]);
bool reading_stale(const reading_t *reading, int64_t now_us);
/* "66°" (UTF-8 degree sign), or "--" when unavailable. */
void format_temperature(const reading_t *reading, char *out, size_t size);
/* Magnitude only: "3.9", "153", "0.0" or "--". */
battery_direction_t format_battery(const reading_t *reading, char *out, size_t size);

#endif
