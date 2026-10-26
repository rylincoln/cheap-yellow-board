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

static void worst(int w, int h)
{
    glance_tile_t tiles[SENSOR_COUNT];
    double temps[4] = {-12.6, 100.2, -999.4, 999.4};
    for (int k = 0; k < 4; k++) {
        reading_t r[SENSOR_COUNT] = {fresh(temps[k]), fresh(temps[k]), fresh(temps[k]), fresh(-153.0)};
        glance_compose(w, h, r, NOW, tiles);
        for (int s = 0; s < SENSOR_COUNT; s++) inside(&tiles[s]);
    }
    double bats[2] = {-153.0, 137.2};
    for (int k = 0; k < 2; k++) {
        reading_t r[SENSOR_COUNT] = {fresh(66), fresh(66), fresh(66), fresh(bats[k])};
        glance_compose(w, h, r, NOW, tiles);
        for (int s = 0; s < SENSOR_COUNT; s++) inside(&tiles[s]);
    }
    reading_t none[SENSOR_COUNT] = {{0}};
    glance_compose(w, h, none, NOW, tiles);
    for (int s = 0; s < SENSOR_COUNT; s++) inside(&tiles[s]);
}

/* The font the outside tile gives its number when outside reads t. */
static const bigfont_t *outside_font(int w, int h, double t)
{
    glance_tile_t tiles[SENSOR_COUNT];
    reading_t r[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(t), fresh(-3.876)};
    glance_compose(w, h, r, NOW, tiles);
    return tiles[SENSOR_OUTSIDE].texts[1].font;
}

/* The font an inside tile gives its number when it reads t. */
static const bigfont_t *upstairs_font(int w, int h, double t)
{
    glance_tile_t tiles[SENSOR_COUNT];
    reading_t r[SENSOR_COUNT] = {fresh(t), fresh(67), fresh(40), fresh(-3.876)};
    glance_compose(w, h, r, NOW, tiles);
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
    glance_tile_t tiles[SENSOR_COUNT];
    reading_t ok[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(-3.876)};

    glance_compose(480, 320, ok, NOW, tiles);
    const rect_t want[SENSOR_COUNT] = {{4, 4, 212, 116}, {4, 124, 212, 116}, {220, 4, 256, 236}, {4, 244, 472, 72}};
    for (int s = 0; s < SENSOR_COUNT; s++) {
        CHECK(tiles[s].rect.x == want[s].x && tiles[s].rect.y == want[s].y);
        CHECK(tiles[s].rect.w == want[s].w && tiles[s].rect.h == want[s].h);
        CHECK(tiles[s].background == glance_rgb(0x14, 0x14, 0x14));
    }
    CHECK(has(&tiles[SENSOR_BATTERY], "\xE2\x96\xBC" "3.9", glance_rgb(0xFF, 0x8A, 0x7A)));
    CHECK(has(&tiles[SENSOR_BATTERY], "draining", glance_rgb(0xFF, 0x8A, 0x7A)));
    CHECK(tiles[SENSOR_OUTSIDE].texts[1].font == &bigfont_outside_480);
    CHECK(tiles[SENSOR_UPSTAIRS].texts[1].font == &bigfont_tile_480);

    glance_compose(320, 240, ok, NOW, tiles);
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

    reading_t stale[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(-3.876)};
    stale[SENSOR_UPSTAIRS].reported_at_us = NOW - READING_STALE_US - 1;
    glance_compose(480, 320, stale, NOW, tiles);
    CHECK(tiles[SENSOR_UPSTAIRS].texts[1].color == glance_rgb(0x70, 0x70, 0x70));
    CHECK(tiles[SENSOR_DOWNSTAIRS].texts[1].color == glance_rgb(0xFF, 0xD3, 0x8A));
    CHECK(tiles[SENSOR_OUTSIDE].texts[1].color == glance_rgb(0x7F, 0xD4, 0xFF));

    reading_t idle[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(0.05)};
    glance_compose(480, 320, idle, NOW, tiles);
    CHECK(has(&tiles[SENSOR_BATTERY], "0.0", glance_rgb(0x70, 0x70, 0x70)));
    CHECK(has(&tiles[SENSOR_BATTERY], "idle", glance_rgb(0x70, 0x70, 0x70)));
    for (int i = 0; i < tiles[SENSOR_BATTERY].count; i++)
        CHECK((unsigned char)tiles[SENSOR_BATTERY].texts[i].text[0] != 0xE2);

    reading_t chg[SENSOR_COUNT] = {fresh(66), fresh(67), fresh(40), fresh(137.2)};
    glance_compose(480, 320, chg, NOW, tiles);
    CHECK(has(&tiles[SENSOR_BATTERY], "\xE2\x96\xB2" "137", glance_rgb(0x8A, 0xE6, 0x8A)));

    /* tiles compare whole with memcmp: padding is zeroed */
    glance_tile_t a[SENSOR_COUNT], b[SENSOR_COUNT];
    memset(a, 0xAA, sizeof a);
    memset(b, 0x55, sizeof b);
    glance_compose(480, 320, ok, NOW, a);
    glance_compose(480, 320, ok, NOW, b);
    CHECK(memcmp(a, b, sizeof a) == 0);
    CHECK_DONE();
}
