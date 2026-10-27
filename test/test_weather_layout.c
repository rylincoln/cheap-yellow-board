#include "check.h"
#include "weather_layout.h"
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
static reading_t gone(void) { reading_t r = {false, 0, NOW}; return r; }
static reading_t old(double v) { reading_t r = {true, v, NOW - READING_STALE_US - 1}; return r; }

static int has(const glance_tile_t *t, const char *text, uint16_t color)
{
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->texts[i].text, text) == 0 && t->texts[i].color == color) return 1;
    return 0;
}

static const glance_text_t *find(const glance_tile_t *t, const char *text)
{
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->texts[i].text, text) == 0) return &t->texts[i];
    return NULL;
}

/* The shed strip's texts are centred as one line. */
static void centred(const glance_tile_t *t)
{
    const glance_text_t *last = &t->texts[t->count - 1];
    int left = t->texts[0].x - t->rect.x;
    int right = t->rect.x + t->rect.w - (last->x + bigfont_text_width(last->font, last->text));
    CHECK(left - right <= 1 && right - left <= 1);
}

/* temp, high, low, rain today, rain this week, shed charge, dawn */
static void set(reading_t r[WX_COUNT], reading_t t, reading_t h, reading_t l, reading_t r1, reading_t r2,
                reading_t s, reading_t d)
{
    r[WX_TEMP] = t; r[WX_HIGH] = h; r[WX_LOW] = l; r[WX_RAIN_TODAY] = r1;
    r[WX_RAIN_WEEK] = r2; r[WX_SHED_PCT] = s; r[WX_SHED_DAWN] = d;
}

static void all_inside(int w, int h, const reading_t r[WX_COUNT])
{
    glance_tile_t tiles[WEATHER_TILE_COUNT];
    weather_compose(w, h, r, NOW, tiles);
    for (int s = 0; s < WEATHER_TILE_COUNT; s++) inside(&tiles[s]);
    centred(&tiles[TILE_SHED]);
}

static void worst(int w, int h)
{
    reading_t r[WX_COUNT];
    double temps[3] = {-10, 100, -100};
    for (int k = 0; k < 3; k++) {
        set(r, fresh(temps[k]), fresh(58), fresh(39), fresh(0.12), fresh(1.48), fresh(82), fresh(64));
        all_inside(w, h, r);
    }
    set(r, gone(), fresh(58), fresh(39), fresh(0.12), fresh(1.48), fresh(82), fresh(64));
    all_inside(w, h, r);
    double hl[2] = {100, -10};
    for (int k = 0; k < 2; k++) {
        set(r, fresh(47), fresh(hl[k]), fresh(hl[k]), fresh(0.12), fresh(1.48), fresh(82), fresh(64));
        all_inside(w, h, r);
    }
    set(r, fresh(47), fresh(58), fresh(39), fresh(99.9), fresh(99.9), fresh(82), fresh(64));
    all_inside(w, h, r);
    set(r, fresh(47), fresh(58), fresh(39), fresh(0.12), fresh(1.48), fresh(100), fresh(100));
    all_inside(w, h, r);
    set(r, fresh(47), fresh(58), fresh(39), fresh(0.12), fresh(1.48), gone(), gone());
    all_inside(w, h, r);
    set(r, gone(), gone(), gone(), gone(), gone(), gone(), gone());
    all_inside(w, h, r);
}

/* The font the outside tile gives its number when outside reads t. */
static const bigfont_t *temp_font(int w, int h, double t)
{
    glance_tile_t tiles[WEATHER_TILE_COUNT];
    reading_t r[WX_COUNT];
    set(r, fresh(t), fresh(58), fresh(39), fresh(0.12), fresh(1.48), fresh(82), fresh(64));
    weather_compose(w, h, r, NOW, tiles);
    return tiles[TILE_OUTSIDE].texts[1].font;
}

static void font_chain(int w, int h, const bigfont_t *temp, const bigfont_t *mid, const bigfont_t *hilo)
{
    CHECK(temp_font(w, h, 47) == temp);
    CHECK(temp_font(w, h, -10) == mid);
    CHECK(temp_font(w, h, 100) == mid);
    CHECK(temp_font(w, h, -100) == hilo);
}

int main(void)
{
    glance_tile_t tiles[WEATHER_TILE_COUNT];
    reading_t r[WX_COUNT];
    uint16_t gray = glance_rgb(0x70, 0x70, 0x70), unit_gray = glance_rgb(0x9A, 0x9A, 0x9A);
    uint16_t blue = glance_rgb(0x7F, 0xD4, 0xFF), amber = glance_rgb(0xFF, 0xD3, 0x8A);
    uint16_t white = glance_rgb(0xE6, 0xE6, 0xE6), red = glance_rgb(0xFF, 0x8A, 0x7A);

    set(r, fresh(47), fresh(58), fresh(39), fresh(0.12), fresh(1.48), fresh(82.4), fresh(64.2));
    weather_compose(480, 320, r, NOW, tiles);
    const rect_t want[WEATHER_TILE_COUNT] = {{4, 4, 288, 236}, {296, 4, 180, 116}, {296, 124, 180, 116}, {4, 244, 472, 72}};
    for (int s = 0; s < WEATHER_TILE_COUNT; s++) {
        CHECK(tiles[s].rect.x == want[s].x && tiles[s].rect.y == want[s].y);
        CHECK(tiles[s].rect.w == want[s].w && tiles[s].rect.h == want[s].h);
        CHECK(tiles[s].background == glance_rgb(0x14, 0x14, 0x14));
    }
    CHECK(has(&tiles[TILE_OUTSIDE], "OUTSIDE", unit_gray));
    CHECK(has(&tiles[TILE_OUTSIDE], "47\xC2\xB0", blue));
    CHECK(has(&tiles[TILE_OUTSIDE], "\xE2\x96\xB2" "58\xC2\xB0", amber));
    CHECK(has(&tiles[TILE_OUTSIDE], "\xE2\x96\xBC" "39\xC2\xB0", blue));
    CHECK(has(&tiles[TILE_OUTSIDE], "PAST 24 HOURS", unit_gray));
    CHECK(has(&tiles[TILE_RAIN_TODAY], "RAIN TODAY", unit_gray));
    CHECK(has(&tiles[TILE_RAIN_TODAY], "0.12", white));
    CHECK(has(&tiles[TILE_RAIN_TODAY], "in", unit_gray));
    CHECK(has(&tiles[TILE_RAIN_WEEK], "THIS WEEK", unit_gray));
    CHECK(has(&tiles[TILE_RAIN_WEEK], "1.48", white));
    CHECK(has(&tiles[TILE_RAIN_WEEK], "in", unit_gray));
    CHECK(tiles[TILE_SHED].count == 7);
    CHECK(has(&tiles[TILE_SHED], "SHED", unit_gray));
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x88" "82", white));
    CHECK(has(&tiles[TILE_SHED], "%", unit_gray));
    CHECK(has(&tiles[TILE_SHED], "\xE2\x86\x92", glance_rgb(0x70, 0x70, 0x70)));
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x90" "64", amber));
    CHECK(has(&tiles[TILE_SHED], "AT DAWN", unit_gray));
    centred(&tiles[TILE_SHED]);
    CHECK(tiles[TILE_OUTSIDE].texts[1].font == &bigfont_temp_480);

    weather_compose(320, 240, r, NOW, tiles);
    CHECK(tiles[TILE_OUTSIDE].rect.x == 3 && tiles[TILE_SHED].rect.y == 183);
    CHECK(tiles[TILE_SHED].rect.w == 315 && tiles[TILE_SHED].rect.h == 54);
    CHECK(tiles[TILE_OUTSIDE].texts[1].font == &bigfont_temp_320);

    worst(480, 320);
    worst(320, 240);
    font_chain(480, 320, &bigfont_temp_480, &bigfont_temp_mid_480, &bigfont_hilo_480);
    font_chain(320, 240, &bigfont_temp_320, &bigfont_temp_mid_320, &bigfont_hilo_320);

    /* old temp and charge go gray; the rest keep their colors */
    set(r, old(47), old(58), old(39), old(0.12), old(1.48), old(82.4), old(64.2));
    weather_compose(480, 320, r, NOW, tiles);
    CHECK(has(&tiles[TILE_OUTSIDE], "47\xC2\xB0", gray));
    CHECK(has(&tiles[TILE_OUTSIDE], "\xE2\x96\xB2" "58\xC2\xB0", amber));
    CHECK(has(&tiles[TILE_OUTSIDE], "\xE2\x96\xBC" "39\xC2\xB0", blue));
    CHECK(has(&tiles[TILE_RAIN_TODAY], "0.12", white));
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x88" "82", gray));
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x90" "64", amber));
    /* stale wins over low */
    set(r, fresh(47), fresh(58), fresh(39), fresh(0.12), fresh(1.48), old(10), fresh(64.2));
    weather_compose(480, 320, r, NOW, tiles);
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x81" "10", gray));

    /* dry and missing rain are gray */
    set(r, fresh(47), fresh(58), fresh(39), fresh(0), gone(), fresh(82.4), fresh(64.2));
    weather_compose(480, 320, r, NOW, tiles);
    CHECK(has(&tiles[TILE_RAIN_TODAY], "0.00", gray));
    CHECK(has(&tiles[TILE_RAIN_WEEK], "--", gray));

    /* red at 20% and under */
    set(r, fresh(47), fresh(58), fresh(39), fresh(0.12), fresh(1.48), fresh(20), fresh(20));
    weather_compose(480, 320, r, NOW, tiles);
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x82" "20", red));
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x90" "20", red));
    r[WX_SHED_PCT] = fresh(21);
    weather_compose(480, 320, r, NOW, tiles);
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x82" "21", white));

    /* missing charge: an empty battery and "--" in gray */
    set(r, fresh(47), fresh(58), fresh(39), fresh(0.12), fresh(1.48), gone(), gone());
    weather_compose(480, 320, r, NOW, tiles);
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x80" "--", gray));
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x90" "--", gray));

    /* a dawn forecast below zero reads 0 in red */
    r[WX_SHED_PCT] = fresh(50);
    r[WX_SHED_DAWN] = fresh(-12);
    weather_compose(480, 320, r, NOW, tiles);
    CHECK(has(&tiles[TILE_SHED], "\xEE\x80\x90" "0", red));

    /* high and low sensors not made yet */
    set(r, fresh(47), gone(), gone(), fresh(0.12), fresh(1.48), fresh(82.4), fresh(64.2));
    weather_compose(480, 320, r, NOW, tiles);
    CHECK(has(&tiles[TILE_OUTSIDE], "\xE2\x96\xB2" "--", gray));
    CHECK(has(&tiles[TILE_OUTSIDE], "\xE2\x96\xBC" "--", gray));
    CHECK(has(&tiles[TILE_OUTSIDE], "47\xC2\xB0", blue));
    CHECK(find(&tiles[TILE_OUTSIDE], "47\xC2\xB0") != NULL);

    /* seven 95-character IDs fit the request buffer */
    char id[96], body[WX_BODY_SIZE];
    memset(id, 'a', 95);
    id[95] = 0;
    const char *ids[WX_COUNT];
    for (int i = 0; i < WX_COUNT; i++) ids[i] = id;
    CHECK(readings_template_body(ids, WX_COUNT, body, sizeof body) > 0);

    /* tiles compare whole with memcmp: padding is zeroed */
    glance_tile_t a[WEATHER_TILE_COUNT], b[WEATHER_TILE_COUNT];
    set(r, fresh(47), fresh(58), fresh(39), fresh(0.12), fresh(1.48), fresh(82.4), fresh(64.2));
    memset(a, 0xAA, sizeof a);
    memset(b, 0x55, sizeof b);
    weather_compose(480, 320, r, NOW, a);
    weather_compose(480, 320, r, NOW, b);
    CHECK(memcmp(a, b, sizeof a) == 0);
    CHECK_DONE();
}
