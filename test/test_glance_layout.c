#include "check.h"
#include "glance_layout.h"
#include "bigfont_data.h"

#define NOW 1000000000LL

static void inside(const glance_tile_t *t)
{
    CHECK(t->count > 0);
    for (int i = 0; i < t->count; i++) {
        const glance_text_t *g = &t->texts[i];
        int w = bigfont_text_width(g->font, g->text);
        if (!(g->x >= t->rect.x && g->x + w <= t->rect.x + t->rect.w &&
              g->line_y >= t->rect.y && g->line_y + g->font->line_height <= t->rect.y + t->rect.h))
            printf("outside: \"%s\" x=%d w=%d line_y=%d in %d,%d,%d,%d\n", g->text, g->x, w, g->line_y,
                   t->rect.x, t->rect.y, t->rect.w, t->rect.h);
        CHECK(g->x >= t->rect.x);
        CHECK(g->x + w <= t->rect.x + t->rect.w);
        CHECK(g->line_y >= t->rect.y);
        CHECK(g->line_y + g->font->line_height <= t->rect.y + t->rect.h);
    }
}

static reading_t fresh(double v) { reading_t r = {true, v, NOW}; return r; }

static int has(const glance_tile_t *t, const char *text, uint16_t color)
{
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->texts[i].text, text) == 0 && t->texts[i].color == color) return 1;
    return 0;
}

/* The battery strip's texts are centred as one line. */
static void centred(const glance_tile_t *t)
{
    const glance_text_t *last = &t->texts[t->count - 1];
    int left = t->texts[0].x - t->rect.x;
    int right = t->rect.x + t->rect.w - (last->x + bigfont_text_width(last->font, last->text));
    CHECK(left - right <= 1 && right - left <= 1);
}

static void worst(int w, int h)
{
    glance_tile_t tiles[GLANCE_TILE_COUNT];
    double temps[4] = {-12.6, 100.2, -999.4, 999.4};
    for (int k = 0; k < 4; k++) {
        reading_t r[SENSOR_COUNT] = {fresh(temps[k]), fresh(temps[k]), fresh(temps[k]), fresh(-153.0), fresh(100)};
        glance_compose(w, h, r, true, NOW, tiles);
        for (int s = 0; s < GLANCE_TILE_COUNT; s++) inside(&tiles[s]);
    }
    double bats[3] = {-153.0, 137.2, -999.4};
    for (int k = 0; k < 3; k++) {
        reading_t r[SENSOR_COUNT] = {fresh(66), fresh(66), fresh(66), fresh(bats[k]), fresh(100)};
        glance_compose(w, h, r, true, NOW, tiles);
        for (int s = 0; s < GLANCE_TILE_COUNT; s++) inside(&tiles[s]);
        centred(&tiles[SENSOR_BATTERY]);
    }
    reading_t none[SENSOR_COUNT] = {{0}};
    glance_compose(w, h, none, true, NOW, tiles);
    for (int s = 0; s < GLANCE_TILE_COUNT; s++) inside(&tiles[s]);
}

/* The battery strip's charge text and color when the charge reads pct. */
static void charge(double pct, const char *want, uint16_t color)
{
    glance_tile_t tiles[GLANCE_TILE_COUNT];
    reading_t r[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(-3.876), fresh(pct)};
    glance_compose(480, 320, r, true, NOW, tiles);
    CHECK(tiles[SENSOR_BATTERY].count == 4);
    CHECK(has(&tiles[SENSOR_BATTERY], want, color));
}

/* The font the outside tile gives its number when outside reads t. */
static const bigfont_t *outside_font(int w, int h, double t)
{
    glance_tile_t tiles[GLANCE_TILE_COUNT];
    reading_t r[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(t), fresh(-3.876), fresh(82.4)};
    glance_compose(w, h, r, true, NOW, tiles);
    return tiles[SENSOR_OUTSIDE].texts[1].font;
}

/* The font an inside tile gives its number when it reads t. */
static const bigfont_t *upstairs_font(int w, int h, double t)
{
    glance_tile_t tiles[GLANCE_TILE_COUNT];
    reading_t r[SENSOR_COUNT] = {fresh(t), fresh(67), fresh(40), fresh(-3.876), fresh(82.4)};
    glance_compose(w, h, r, true, NOW, tiles);
    return tiles[SENSOR_UPSTAIRS].texts[1].font;
}

/* Outside steps down outside, outside_mid, tile as its number widens, so
   -10 and 100 stay larger than the inside tiles. Inside tiles step down
   tile, strip. */
static void font_chain(int w, int h, const bigfont_t *outside, const bigfont_t *mid,
                       const bigfont_t *tile, const bigfont_t *strip)
{
    CHECK(outside_font(w, h, 47) == outside);
    CHECK(outside_font(w, h, -10) == mid);
    CHECK(outside_font(w, h, 100) == mid);
    CHECK(outside_font(w, h, -100) == tile);
    CHECK(upstairs_font(w, h, 66) == tile);
    CHECK(upstairs_font(w, h, -999) == strip);
}

int main(void)
{
    glance_tile_t tiles[GLANCE_TILE_COUNT];
    reading_t ok[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(-3.876), fresh(82.4)};
    uint16_t red = glance_rgb(0xFF, 0x8A, 0x7A), gray = glance_rgb(0x70, 0x70, 0x70);
    uint16_t unit_gray = glance_rgb(0x9A, 0x9A, 0x9A), white = glance_rgb(0xE6, 0xE6, 0xE6);

    glance_compose(480, 320, ok, true, NOW, tiles);
    const rect_t want[GLANCE_TILE_COUNT] = {{4, 4, 212, 116}, {4, 124, 212, 116}, {220, 4, 256, 236}, {4, 244, 472, 72}};
    for (int s = 0; s < GLANCE_TILE_COUNT; s++) {
        CHECK(tiles[s].rect.x == want[s].x && tiles[s].rect.y == want[s].y);
        CHECK(tiles[s].rect.w == want[s].w && tiles[s].rect.h == want[s].h);
        CHECK(tiles[s].background == glance_rgb(0x14, 0x14, 0x14));
    }
    /* the current with its arrow, "A", the battery icon filled 8 tenths with "82", "%" */
    CHECK(tiles[SENSOR_BATTERY].count == 4);
    CHECK(has(&tiles[SENSOR_BATTERY], "\xE2\x96\xBC" "3.9", red));
    CHECK(has(&tiles[SENSOR_BATTERY], "A", unit_gray));
    CHECK(has(&tiles[SENSOR_BATTERY], "\xEE\x80\x88" "82", white));
    CHECK(has(&tiles[SENSOR_BATTERY], "%", unit_gray));
    centred(&tiles[SENSOR_BATTERY]);
    CHECK(tiles[SENSOR_OUTSIDE].texts[1].font == &bigfont_outside_480);
    CHECK(tiles[SENSOR_UPSTAIRS].texts[1].font == &bigfont_tile_480);

    glance_compose(320, 240, ok, true, NOW, tiles);
    CHECK(tiles[SENSOR_UPSTAIRS].rect.x == 3 && tiles[SENSOR_UPSTAIRS].rect.y == 3);
    CHECK(tiles[SENSOR_UPSTAIRS].rect.w == 141 && tiles[SENSOR_UPSTAIRS].rect.h == 87);
    CHECK(tiles[SENSOR_BATTERY].rect.x == 3 && tiles[SENSOR_BATTERY].rect.y == 183);
    CHECK(tiles[SENSOR_BATTERY].rect.w == 315 && tiles[SENSOR_BATTERY].rect.h == 54);
    CHECK(tiles[SENSOR_UPSTAIRS].texts[1].font == &bigfont_tile_320);

    font_chain(480, 320, &bigfont_outside_480, &bigfont_outside_mid_480, &bigfont_tile_480,
               &bigfont_strip_480);
    font_chain(320, 240, &bigfont_outside_320, &bigfont_outside_mid_320, &bigfont_tile_320,
               &bigfont_strip_320);

    worst(480, 320);
    worst(320, 240);

    /* without a charge sensor, only the current, centred */
    glance_compose(480, 320, ok, false, NOW, tiles);
    CHECK(tiles[SENSOR_BATTERY].count == 2);
    centred(&tiles[SENSOR_BATTERY]);
    glance_compose(320, 240, ok, false, NOW, tiles);
    CHECK(tiles[SENSOR_BATTERY].count == 2);
    centred(&tiles[SENSOR_BATTERY]);

    /* red at 20% and under; the icon fills to the nearest tenth */
    charge(100, "\xEE\x80\x8A" "100", white);
    charge(21, "\xEE\x80\x82" "21", white);
    charge(20, "\xEE\x80\x82" "20", red);
    charge(4, "\xEE\x80\x80" "4", red);
    charge(101, "\xEE\x80\x80" "--", gray);

    reading_t stale[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(-3.876), fresh(82.4)};
    stale[SENSOR_UPSTAIRS].reported_at_us = NOW - READING_STALE_US - 1;
    stale[SENSOR_BATTERY_PCT].reported_at_us = NOW - READING_STALE_US - 1;
    glance_compose(480, 320, stale, true, NOW, tiles);
    CHECK(has(&tiles[SENSOR_BATTERY], "\xEE\x80\x88" "82", gray));
    stale[SENSOR_BATTERY_PCT].available = false;
    glance_compose(480, 320, stale, true, NOW, tiles);
    CHECK(has(&tiles[SENSOR_BATTERY], "\xEE\x80\x80" "--", gray));
    CHECK(tiles[SENSOR_UPSTAIRS].texts[1].color == gray);
    CHECK(tiles[SENSOR_DOWNSTAIRS].texts[1].color == glance_rgb(0xFF, 0xD3, 0x8A));
    CHECK(tiles[SENSOR_OUTSIDE].texts[1].color == glance_rgb(0x7F, 0xD4, 0xFF));

    /* idle: no arrow or bolt */
    reading_t idle[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(0.05), fresh(82.4)};
    glance_compose(480, 320, idle, true, NOW, tiles);
    CHECK(has(&tiles[SENSOR_BATTERY], "0.0", gray));
    for (int i = 0; i < tiles[SENSOR_BATTERY].count; i++)
        CHECK((unsigned char)tiles[SENSOR_BATTERY].texts[i].text[0] != 0xE2);

    reading_t chg[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(137.2), fresh(82.4)};
    glance_compose(480, 320, chg, true, NOW, tiles);
    CHECK(has(&tiles[SENSOR_BATTERY], "\xE2\x9A\xA1" "137", glance_rgb(0x8A, 0xE6, 0x8A)));

    /* tiles compare whole with memcmp: padding is zeroed */
    glance_tile_t a[GLANCE_TILE_COUNT], b[GLANCE_TILE_COUNT];
    memset(a, 0xAA, sizeof a);
    memset(b, 0x55, sizeof b);
    glance_compose(480, 320, ok, true, NOW, a);
    glance_compose(480, 320, ok, true, NOW, b);
    CHECK(memcmp(a, b, sizeof a) == 0);
    CHECK_DONE();
}
