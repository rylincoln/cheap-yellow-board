#include "readings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A temperature or current this large, either way, has more than three
   digits and shows as "--". */
#define DISPLAY_LIMIT 999.5

int readings_template_body(const char *const entity_ids[SENSOR_COUNT], char *out, size_t size)
{
    int n = snprintf(out, size, "{\"template\":\"{%% for s in expand([");
    bool first = true;
    for (int i = 0; i < SENSOR_COUNT && n >= 0 && (size_t)n < size; i++) {
        if (entity_ids[i][0] != '\0') {
            n += snprintf(out + n, size - n, "%s'%s'", first ? "" : ",", entity_ids[i]);
            first = false;
        }
    }
    if (n >= 0 && (size_t)n < size) {
        n += snprintf(out + n, size - n,
                      "]) %%}{{ s.entity_id }} {{ s.state }} "
                      "{{ (now() - s.last_reported).total_seconds() | int }}"
                      "\\n{%% endfor %%}\"}");
    }
    return (n < 0 || (size_t)n >= size) ? -1 : n;
}

/* Parses one line (no newline, no CR) into reading_t if it is good. */
static void apply_line(const char *line, size_t len, const char *const ids[SENSOR_COUNT],
                       int64_t now_us, reading_t readings[SENSOR_COUNT], bool seen[SENSOR_COUNT])
{
    const char *first = memchr(line, ' ', len);
    if (!first) {
        return;
    }
    const char *last = line + len;
    while (last > line && *(last - 1) != ' ') {
        last--;
    }
    last--; /* the final space */
    if (last <= first) {
        return;
    }
    size_t id_len = (size_t)(first - line);
    const char *age_s = last + 1;
    size_t age_len = (size_t)(line + len - age_s);
    if (age_len == 0 || age_len > 12) {
        return;
    }
    int64_t age = 0;
    for (size_t i = 0; i < age_len; i++) {
        if (age_s[i] < '0' || age_s[i] > '9') {
            return;
        }
        age = age * 10 + (age_s[i] - '0');
    }
    char state[48];
    size_t state_len = (size_t)(last - first - 1);
    if (state_len == 0 || state_len >= sizeof(state)) {
        return;
    }
    memcpy(state, first + 1, state_len);
    state[state_len] = '\0';
    char *end;
    double value = strtod(state, &end);
    if (*end != '\0' || !isfinite(value)) {
        return;
    }
    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (!seen[i] && id_len > 0 && strlen(ids[i]) == id_len && memcmp(ids[i], line, id_len) == 0) {
            readings[i].available = true;
            readings[i].value = value;
            readings[i].reported_at_us = now_us - age * 1000000LL;
            seen[i] = true;
            return;
        }
    }
}

int readings_apply(const char *reply, const char *const entity_ids[SENSOR_COUNT],
                   int64_t now_us, reading_t readings[SENSOR_COUNT])
{
    bool seen[SENSOR_COUNT] = {false};
    for (int i = 0; i < SENSOR_COUNT; i++) {
        readings[i].available = false;
    }
    const char *p = reply;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        size_t content = len;
        if (content > 0 && p[content - 1] == '\r') {
            content--;
        }
        apply_line(p, content, entity_ids, now_us, readings, seen);
        p += len + (nl ? 1 : 0);
    }
    int count = 0;
    for (int i = 0; i < SENSOR_COUNT; i++) {
        count += readings[i].available;
    }
    return count;
}

bool reading_stale(const reading_t *reading, int64_t now_us)
{
    return !reading->available || now_us - reading->reported_at_us > READING_STALE_US;
}

void format_temperature(const reading_t *reading, char *out, size_t size)
{
    /* Checked before lround, whose result past a long is unspecified; the
       negated test also turns away NaN. */
    if (!reading->available || !(fabs(reading->value) < DISPLAY_LIMIT)) {
        snprintf(out, size, "--");
        return;
    }
    snprintf(out, size, "%ld\xC2\xB0", lround(reading->value));
}

battery_direction_t format_battery(const reading_t *reading, char *out, size_t size)
{
    if (!reading->available) {
        snprintf(out, size, "--");
        return BATTERY_UNKNOWN;
    }
    double a = fabs(reading->value);
    if (!(a < DISPLAY_LIMIT)) { /* before lround, as for temperatures */
        snprintf(out, size, "--");
        return BATTERY_UNKNOWN;
    }
    if (a < 0.1) {
        snprintf(out, size, "0.0");
        return BATTERY_IDLE;
    }
    double tenth = round(a * 10.0) / 10.0;
    if (tenth < 100.0) {
        snprintf(out, size, "%.1f", tenth);
    } else {
        snprintf(out, size, "%ld", lround(a));
    }
    return reading->value < 0 ? BATTERY_DRAINING : BATTERY_CHARGING;
}

int format_percent(const reading_t *reading, char *out, size_t size)
{
    /* The negated test also turns away NaN. */
    if (!reading->available || !(reading->value >= -0.5 && reading->value < 100.5)) {
        snprintf(out, size, "--");
        return -1;
    }
    int percent = (int)lround(reading->value);
    if (percent < 0) {
        percent = 0; /* -0.5 rounds away from zero */
    }
    snprintf(out, size, "%d", percent);
    return percent;
}
