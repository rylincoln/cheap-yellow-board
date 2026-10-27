#include "weather_layout.h"

#include <stdio.h>
#include <string.h>

#include "bigfont_data.h"

/* The layout is drawn on this grid and scaled to other panels. */
#define DESIGN_W 480
#define DESIGN_H 320

#define TILE_PAD 16 /* a number must be this much narrower than its tile */

static const rect_t design_rects[WEATHER_TILE_COUNT] = {
    [TILE_OUTSIDE] = {4, 4, 288, 236},
    [TILE_RAIN_TODAY] = {296, 4, 180, 116},
    [TILE_RAIN_WEEK] = {296, 124, 180, 116},
    [TILE_SHED] = {4, 244, 472, 72},
};

typedef struct {
    const bigfont_t *label, *small, *hilo, *rain, *shed, *temp, *temp_mid;
} fonts_t;

static const fonts_t fonts_480 = {&bigfont_label_480, &bigfont_small_480, &bigfont_hilo_480, &bigfont_rain_480,
                                  &bigfont_shed_480, &bigfont_temp_480, &bigfont_temp_mid_480};
static const fonts_t fonts_320 = {&bigfont_label_320, &bigfont_small_320, &bigfont_hilo_320, &bigfont_rain_320,
                                  &bigfont_shed_320, &bigfont_temp_320, &bigfont_temp_mid_320};

#define GRAY glance_rgb(0x70, 0x70, 0x70)
#define UNIT_GRAY glance_rgb(0x9A, 0x9A, 0x9A)
#define BLUE glance_rgb(0x7F, 0xD4, 0xFF)
#define AMBER glance_rgb(0xFF, 0xD3, 0x8A)
#define WHITE glance_rgb(0xE6, 0xE6, 0xE6)
#define RED glance_rgb(0xFF, 0x8A, 0x7A)

static int centred_x(const rect_t *r, const bigfont_t *font, const char *text)
{
    return r->x + (r->w - bigfont_text_width(font, text)) / 2;
}

/* The label at the top of a tile; returns the line below it. */
static int add_label(glance_tile_t *tile, const fonts_t *f, const char *label, bool small_panel)
{
    int y = tile->rect.y + (small_panel ? 4 : 6);
    glance_add_text(tile, f->label, label, centred_x(&tile->rect, f->label, label), y, UNIT_GRAY);
    return y + f->label->line_height;
}

/* Two strings side by side, centred together on one baseline. */
static void add_pair(glance_tile_t *tile, const bigfont_t *fa, const char *a, uint16_t ca, const bigfont_t *fb,
                     const char *b, uint16_t cb, int gap, int top, int bottom)
{
    int total = bigfont_text_width(fa, a) + gap + bigfont_text_width(fb, b);
    int x = tile->rect.x + (tile->rect.w - total) / 2;
    int centre = (top + bottom) / 2;
    int baseline = centre - fa->ascent / 2 + fa->ascent; /* digits centred */
    if (baseline - fa->ascent < top) baseline = top + fa->ascent;
    glance_add_text(tile, fa, a, x, baseline - fa->ascent, ca);
    glance_add_text(tile, fb, b, x + bigfont_text_width(fa, a) + gap, baseline - fb->ascent, cb);
}

/* The big temperature, its day's high and low, and what they span. */
static void outside_tile(glance_tile_t *tile, const fonts_t *f, bool small_panel, const reading_t readings[WX_COUNT],
                         int64_t now_us)
{
    const rect_t *r = &tile->rect;
    const int bottom_pad = small_panel ? 6 : 8;
    const int gap = small_panel ? 16 : 24;
    char number[16], high[16], low[16], high_text[24], low_text[24];

    int space_top = add_label(tile, f, "OUTSIDE", small_panel);

    format_temperature(&readings[WX_TEMP], number, sizeof number);
    const bigfont_t *chain[3] = {f->temp, f->temp_mid, f->hilo};
    int pick = 0;
    while (pick < 2 && bigfont_text_width(chain[pick], number) > r->w - TILE_PAD) pick++;
    const bigfont_t *font = chain[pick];

    /* From the bottom up: PAST 24 HOURS, then the high and low. */
    int small_y = r->y + r->h - bottom_pad - f->small->line_height;
    int hilo_y = small_y - f->hilo->line_height;
    int space_bottom = hilo_y;
    int line_y = (space_top + space_bottom) / 2 - font->ascent / 2;
    if (line_y + font->line_height > space_bottom) line_y = space_bottom - font->line_height;
    if (line_y < space_top) line_y = space_top;
    uint16_t color = BLUE;
    if (strcmp(number, "--") == 0 || reading_stale(&readings[WX_TEMP], now_us)) color = GRAY;
    glance_add_text(tile, font, number, centred_x(r, font, number), line_y, color);

    format_temperature(&readings[WX_HIGH], high, sizeof high);
    format_temperature(&readings[WX_LOW], low, sizeof low);
    snprintf(high_text, sizeof high_text, "\xE2\x96\xB2%s", high);
    snprintf(low_text, sizeof low_text, "\xE2\x96\xBC%s", low);
    uint16_t high_color = strcmp(high, "--") == 0 ? GRAY : AMBER;
    uint16_t low_color = strcmp(low, "--") == 0 ? GRAY : BLUE;
    add_pair(tile, f->hilo, high_text, high_color, f->hilo, low_text, low_color, gap, hilo_y, hilo_y + f->hilo->line_height);

    glance_add_text(tile, f->small, "PAST 24 HOURS", centred_x(r, f->small, "PAST 24 HOURS"), small_y, UNIT_GRAY);
}

/* A label, then the inches and a small "in". */
static void rain_tile(glance_tile_t *tile, const fonts_t *f, bool small_panel, const char *label,
                      const reading_t *reading)
{
    char number[16];
    int rain = format_rain(reading, number, sizeof number);
    int space_top = add_label(tile, f, label, small_panel);
    uint16_t color = rain > 0 ? WHITE : GRAY;
    add_pair(tile, f->rain, number, color, f->label, "in", UNIT_GRAY, small_panel ? 6 : 8, space_top,
             tile->rect.y + tile->rect.h);
}

/* "<battery or sunrise><percent>" and its color: gray when missing, red at
   20% and under. */
static uint16_t charge_text(char *out, size_t size, const char *icon, int percent, const char *digits,
                            uint16_t fine, bool dim)
{
    snprintf(out, size, "%s%s", icon, digits);
    if (percent < 0 || dim) return GRAY;
    return percent <= 20 ? RED : fine;
}

/* One line: SHED, the charge and the dawn forecast. */
static void shed_tile(glance_tile_t *tile, const fonts_t *f, bool small_panel, const reading_t readings[WX_COUNT],
                      int64_t now_us)
{
    const rect_t *r = &tile->rect;
    const int gap = small_panel ? 4 : 6; /* six gaps; the widest strip still keeps 8 px at each side */
    char digits[8], icon[4], level[16], dawn[16];

    /* The battery icon filled to the nearest tenth is U+E000 + tenths. */
    int percent = format_percent(&readings[WX_SHED_PCT], digits, sizeof digits);
    int tenths = percent < 0 ? 0 : (percent + 5) / 10;
    snprintf(icon, sizeof icon, "\xEE\x80%c", 0x80 + tenths);
    uint16_t level_color = charge_text(level, sizeof level, icon, percent, digits, WHITE,
                                       reading_stale(&readings[WX_SHED_PCT], now_us));

    /* A forecast below zero means the battery runs out: read 0. */
    reading_t forecast = readings[WX_SHED_DAWN];
    if (forecast.available && forecast.value < -0.5) forecast.value = 0;
    int dawn_percent = format_percent(&forecast, digits, sizeof digits);
    uint16_t dawn_color = charge_text(dawn, sizeof dawn, "\xEE\x80\x90", dawn_percent, digits, AMBER, false);

    const bigfont_t *fonts[7] = {f->small, f->shed, f->label, f->shed, f->shed, f->label, f->small};
    const char *strings[7] = {"SHED", level, "%", "\xE2\x86\x92", dawn, "%", "AT DAWN"};
    uint16_t colors[7] = {UNIT_GRAY, level_color, UNIT_GRAY, GRAY, dawn_color, UNIT_GRAY, UNIT_GRAY};
    int total = 0;
    for (int i = 0; i < 7; i++) total += bigfont_text_width(fonts[i], strings[i]) + (i ? gap : 0);

    int baseline = r->y + r->h / 2 - f->shed->ascent / 2 + f->shed->ascent;
    int x = r->x + (r->w - total) / 2;
    for (int i = 0; i < 7; i++) {
        if (i) x += gap;
        glance_add_text(tile, fonts[i], strings[i], x, baseline - fonts[i]->ascent, colors[i]);
        x += bigfont_text_width(fonts[i], strings[i]);
    }
}

void weather_compose(int width, int height, const reading_t readings[WX_COUNT], int64_t now_us,
                     glance_tile_t tiles[WEATHER_TILE_COUNT])
{
    memset(tiles, 0, sizeof(glance_tile_t) * WEATHER_TILE_COUNT);
    bool small_panel = width != DESIGN_W;
    const fonts_t *f = small_panel ? &fonts_320 : &fonts_480;
    for (int t = 0; t < WEATHER_TILE_COUNT; t++) {
        tiles[t].rect = glance_scale_rect(design_rects[t], width, height);
        tiles[t].background = glance_rgb(0x14, 0x14, 0x14);
    }
    outside_tile(&tiles[TILE_OUTSIDE], f, small_panel, readings, now_us);
    rain_tile(&tiles[TILE_RAIN_TODAY], f, small_panel, "RAIN TODAY", &readings[WX_RAIN_TODAY]);
    rain_tile(&tiles[TILE_RAIN_WEEK], f, small_panel, "THIS WEEK", &readings[WX_RAIN_WEEK]);
    shed_tile(&tiles[TILE_SHED], f, small_panel, readings, now_us);
}
