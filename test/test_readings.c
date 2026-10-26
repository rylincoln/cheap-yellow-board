#include "check.h"
#include "readings.h"

#include <math.h>

#define NOW 1000000000LL

static void temp(double v, bool avail, const char *want)
{
    reading_t r = {avail, v, NOW};
    char buf[16];
    format_temperature(&r, buf, sizeof(buf));
    CHECK_STR(buf, want);
}

static void batt(double v, bool avail, const char *want, battery_direction_t dir)
{
    reading_t r = {avail, v, NOW};
    char buf[16];
    battery_direction_t d = format_battery(&r, buf, sizeof(buf));
    CHECK_STR(buf, want);
    CHECK(d == dir);
}

static void pct(double v, bool avail, const char *want, int percent)
{
    reading_t r = {avail, v, NOW};
    char buf[16];
    CHECK(format_percent(&r, buf, sizeof(buf)) == percent);
    CHECK_STR(buf, want);
}

int main(void)
{
    /* The battery's charge is optional; an empty ID is left out. */
    const char *const ids[SENSOR_COUNT] = {"sensor.a", "sensor.b", "sensor.c", "sensor.d", ""};
    const char *const all_ids[SENSOR_COUNT] = {"sensor.a", "sensor.b", "sensor.c", "sensor.d", "sensor.e"};
    char body[512], small[20];
    reading_t r[SENSOR_COUNT] = {{0}};
    int n;

    int len = readings_template_body(ids, body, sizeof(body));
    CHECK_STR(body, "{\"template\":\"{% for s in expand(['sensor.a','sensor.b','sensor.c','sensor.d']) %}"
                    "{{ s.entity_id }} {{ s.state }} {{ (now() - s.last_reported).total_seconds() | int }}"
                    "\\n{% endfor %}\"}");
    CHECK(len == (int)strlen(body));
    CHECK(readings_template_body(ids, small, 20) == -1);
    readings_template_body(all_ids, body, sizeof(body));
    CHECK(strstr(body, "expand(['sensor.a','sensor.b','sensor.c','sensor.d','sensor.e'])") != NULL);

    n = readings_apply("sensor.c 47.3 5\r\nsensor.a 64.2 14\nsensor.b unavailable 30\ngarbage\nsensor.d -3.876 0", ids, NOW, r);
    CHECK(n == 3);
    CHECK(r[0].available && r[0].value == 64.2 && r[0].reported_at_us == NOW - 14000000LL);
    CHECK(!r[1].available);
    CHECK(r[2].available && r[2].value == 47.3);
    CHECK(r[3].available && r[3].value == -3.876);
    n = readings_apply("sensor.a 64.2 14\n", ids, NOW, r);
    CHECK(n == 1 && !r[3].available);
    n = readings_apply("sensor.a 1 -4\nsensor.b 2 x\nsensor.c 3x 4\nsensor.d  5", ids, NOW, r);
    CHECK(n == 0);
    n = readings_apply(" 7 3\nsensor.e 82.4 3", ids, NOW, r);
    CHECK(n == 0 && !r[SENSOR_BATTERY_PCT].available);
    n = readings_apply("sensor.e 82.4 3", all_ids, NOW, r);
    CHECK(n == 1 && r[SENSOR_BATTERY_PCT].available && r[SENSOR_BATTERY_PCT].value == 82.4);
    r[0] = (reading_t){true, 64.2, NOW - 299000000LL}; CHECK(!reading_stale(&r[0], NOW));
    r[0].reported_at_us = NOW - 301000000LL;           CHECK(reading_stale(&r[0], NOW));
    r[0].available = false; r[0].reported_at_us = NOW;  CHECK(reading_stale(&r[0], NOW));

    temp(64.2, true, "64\xC2\xB0");
    temp(65.5, true, "66\xC2\xB0");
    temp(-0.4, true, "0\xC2\xB0");
    temp(-12.6, true, "-13\xC2\xB0");
    temp(100.2, true, "100\xC2\xB0");
    temp(0, false, "--");
    temp(999.4, true, "999\xC2\xB0");
    temp(-999.4, true, "-999\xC2\xB0");
    temp(999.5, true, "--");
    temp(1e30, true, "--");
    temp(-1e30, true, "--");

    batt(-3.876, true, "3.9", BATTERY_DRAINING);
    batt(90.52, true, "90.5", BATTERY_CHARGING);
    batt(-0.05, true, "0.0", BATTERY_IDLE);
    batt(99.94, true, "99.9", BATTERY_CHARGING);
    batt(99.96, true, "100", BATTERY_CHARGING);
    batt(-153.0, true, "153", BATTERY_DRAINING);
    batt(999.4, true, "999", BATTERY_CHARGING);
    batt(1075.1, true, "--", BATTERY_UNKNOWN);
    batt(0, false, "--", BATTERY_UNKNOWN);
    batt(1e30, true, "--", BATTERY_UNKNOWN);
    batt(-1e30, true, "--", BATTERY_UNKNOWN);

    pct(82.4, true, "82", 82);
    pct(99.6, true, "100", 100);
    pct(100.4, true, "100", 100);
    pct(-0.4, true, "0", 0);
    pct(4.5, true, "5", 5);
    pct(100.5, true, "--", -1);
    pct(-0.6, true, "--", -1);
    pct(50, false, "--", -1);
    pct(1e30, true, "--", -1);
    pct(NAN, true, "--", -1);
    CHECK_DONE();
}
