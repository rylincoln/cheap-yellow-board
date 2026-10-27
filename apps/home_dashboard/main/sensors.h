/* The home dashboard's sensors, in the order of its readings and tiles. */
#ifndef SENSORS_H
#define SENSORS_H

/* SENSOR_BATTERY is the battery's current; SENSOR_BATTERY_PCT, its charge, is optional. */
typedef enum {
    SENSOR_UPSTAIRS, SENSOR_DOWNSTAIRS, SENSOR_OUTSIDE, SENSOR_BATTERY, SENSOR_BATTERY_PCT, SENSOR_COUNT
} sensor_t;

#endif
