#include "glance_layout.h"

#include <stdio.h>
#include <string.h>

#include "bigfont_data.h"

/* The layout is drawn on this grid and scaled to other panels. */
#define DESIGN_W 480
#define DESIGN_H 320

#define TILE_PAD 16 /* a number must be this much narrower than its tile */

static const rect_t design_rects[GLANCE_TILE_COUNT] = {
    [SENSOR_UPSTAIRS] = {4, 4, 212, 116},
    [SENSOR_DOWNSTAIRS] = {4, 124, 212, 116},
    [SENSOR_OUTSIDE] = {220, 4, 256, 236},
    [SENSOR_BATTERY] = {4, 244, 472, 72},
};

static const char *const labels[SENSOR_BATTERY] = {"UPSTAIRS", "DOWNSTAIRS", "OUTSIDE"};

uint16_t glance_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* value * num / den, rounded to the nearest integer (value, num >= 0). */
static int scale(int value, int num, int den)
{
    return (int)(((long)value * num * 2 + den) / (den * 2));
}

static void add_text(glance_tile_t *tile, const bigfont_t *font, const char *text, int x,
                     int line_y, uint16_t color)
{
    glance_text_t *t = &tile->texts[tile->count++];
    t->font = font;
    snprintf(t->text, sizeof t->text, "%s", text);
    t->x = (int16_t)x;
    t->line_y = (int16_t)line_y;
    t->color = color;
}

/* A label and a number. The number takes the largest font that fits:
   Outside steps down outside, outside_mid, tile, strip, and the inside
   tiles start at tile. */
static void temperature_tile(glance_tile_t *tile, sensor_t sensor, bool small_panel,
                             const reading_t *reading, int64_t now_us)
{
    const bigfont_t *label_font = small_panel ? &bigfont_label_320 : &bigfont_label_480;
    const bigfont_t *fonts[4] = {
        small_panel ? &bigfont_outside_320 : &bigfont_outside_480,
        small_panel ? &bigfont_outside_mid_320 : &bigfont_outside_mid_480,
        small_panel ? &bigfont_tile_320 : &bigfont_tile_480,
        small_panel ? &bigfont_strip_320 : &bigfont_strip_480,
    };
    const rect_t *r = &tile->rect;
    uint16_t gray = glance_rgb(0x70, 0x70, 0x70);
    char number[16];

    format_temperature(reading, number, sizeof number);
    add_text(tile, label_font, labels[sensor], r->x + (r->w - bigfont_text_width(label_font, labels[sensor])) / 2,
             r->y + (small_panel ? 4 : 6), glance_rgb(0x9A, 0x9A, 0x9A));

    int pick = sensor == SENSOR_OUTSIDE ? 0 : 2;
    while (pick < 3 && bigfont_text_width(fonts[pick], number) > r->w - TILE_PAD) pick++;
    const bigfont_t *font = fonts[pick];

    int space_top = tile->texts[0].line_y + label_font->line_height;
    int space_bottom = r->y + r->h;
    int centre = (space_top + space_bottom) / 2;
    int line_y = centre - font->ascent / 2; /* digits centred */
    if (line_y + font->line_height > space_bottom) line_y = space_bottom - font->line_height;
    if (line_y < space_top) line_y = space_top;

    uint16_t color = sensor == SENSOR_OUTSIDE ? glance_rgb(0x7F, 0xD4, 0xFF) : glance_rgb(0xFF, 0xD3, 0x8A);
    if (!reading->available || reading_stale(reading, now_us)) color = gray;
    add_text(tile, font, number, r->x + (r->w - bigfont_text_width(font, number)) / 2, line_y, color);
}

/* One line: the current, after a bolt when charging or an arrow when
   draining, then with show_charge a battery icon filled to the charge and
   its percent. The units are small and gray. */
static void battery_tile(glance_tile_t *tile, bool small_panel, const reading_t *current,
                         const reading_t *charge, bool show_charge, int64_t now_us)
{
    const bigfont_t *label_font = small_panel ? &bigfont_label_320 : &bigfont_label_480;
    const bigfont_t *strip_font = small_panel ? &bigfont_strip_320 : &bigfont_strip_480;
    const int unit_gap = small_panel ? 4 : 6;
    const int charge_gap = small_panel ? 32 : 48;
    const rect_t *r = &tile->rect;
    uint16_t gray = glance_rgb(0x70, 0x70, 0x70);
    uint16_t red = glance_rgb(0xFF, 0x8A, 0x7A);

    char magnitude[8], amps[16]; /* icon (3) + magnitude (7) + NUL fits */
    battery_direction_t dir = format_battery(current, magnitude, sizeof magnitude);
    const char *icon = dir == BATTERY_DRAINING ? "\xE2\x96\xBC" : dir == BATTERY_CHARGING ? "\xE2\x9A\xA1" : "";
    snprintf(amps, sizeof amps, "%s%s", icon, magnitude);
    uint16_t amps_color = gray;
    if (!reading_stale(current, now_us)) {
        if (dir == BATTERY_DRAINING) amps_color = red;
        else if (dir == BATTERY_CHARGING) amps_color = glance_rgb(0x8A, 0xE6, 0x8A);
    }

    /* The battery icon filled to the nearest tenth is U+E000 + tenths. */
    char digits[8], level[16];
    int percent = format_percent(charge, digits, sizeof digits);
    int tenths = percent < 0 ? 0 : (percent + 5) / 10;
    snprintf(level, sizeof level, "\xEE\x80%c%s", 0x80 + tenths, digits);
    uint16_t charge_color = gray;
    if (percent >= 0 && !reading_stale(charge, now_us)) {
        charge_color = percent <= 20 ? red : glance_rgb(0xE6, 0xE6, 0xE6);
    }

    uint16_t unit_gray = glance_rgb(0x9A, 0x9A, 0x9A);
    const bigfont_t *fonts[4] = {strip_font, label_font, strip_font, label_font};
    const char *strings[4] = {amps, "A", level, "%"};
    uint16_t colors[4] = {amps_color, unit_gray, charge_color, unit_gray};
    const int gaps[3] = {unit_gap, charge_gap, unit_gap};
    int count = show_charge ? 4 : 2;
    int total = 0;
    for (int i = 0; i < count; i++) total += bigfont_text_width(fonts[i], strings[i]) + (i ? gaps[i - 1] : 0);

    int baseline = r->y + r->h / 2 - strip_font->ascent / 2 + strip_font->ascent;
    int x = r->x + (r->w - total) / 2;
    for (int i = 0; i < count; i++) {
        if (i) x += gaps[i - 1];
        add_text(tile, fonts[i], strings[i], x, baseline - fonts[i]->ascent, colors[i]);
        x += bigfont_text_width(fonts[i], strings[i]);
    }
}

void glance_compose(int width, int height, const reading_t readings[SENSOR_COUNT], bool show_charge,
                    int64_t now_us, glance_tile_t tiles[GLANCE_TILE_COUNT])
{
    memset(tiles, 0, sizeof(glance_tile_t) * GLANCE_TILE_COUNT);
    bool small_panel = !(width == DESIGN_W && height == DESIGN_H);
    for (int s = 0; s < GLANCE_TILE_COUNT; s++) {
        const rect_t *d = &design_rects[s];
        glance_tile_t *tile = &tiles[s];
        tile->rect.x = (int16_t)scale(d->x, width, DESIGN_W);
        tile->rect.y = (int16_t)scale(d->y, height, DESIGN_H);
        tile->rect.w = (int16_t)scale(d->w, width, DESIGN_W);
        tile->rect.h = (int16_t)scale(d->h, height, DESIGN_H);
        tile->background = glance_rgb(0x14, 0x14, 0x14);
        if (s == SENSOR_BATTERY) battery_tile(tile, small_panel, &readings[s], &readings[SENSOR_BATTERY_PCT], show_charge, now_us);
        else temperature_tile(tile, (sensor_t)s, small_panel, &readings[s], now_us);
    }
}
