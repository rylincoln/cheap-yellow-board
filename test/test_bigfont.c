#include "check.h"
#include "bigfont.h"
#include "bigfont_data.h"

#define W 200
#define H 160

static const bigfont_t *const all[8] = {
    &bigfont_label_480, &bigfont_strip_480, &bigfont_tile_480, &bigfont_outside_480,
    &bigfont_label_320, &bigfont_strip_320, &bigfont_tile_320, &bigfont_outside_320,
};

static int advance_of(const bigfont_t *f, uint32_t code)
{
    for (int i = 0; i < f->glyph_count; i++)
        if (f->glyphs[i].code == code) return f->glyphs[i].advance;
    return -1;
}

static int ch(uint16_t p, int s) { return s == 0 ? p >> 11 : s == 1 ? (p >> 5) & 63 : p & 31; }

static uint16_t buf[W * H];
static uint16_t a[W * H], b[W * H];

int main(void)
{
    CHECK(bigfont_text_width(&bigfont_tile_480, "88") == 2 * advance_of(&bigfont_tile_480, '8'));
    CHECK(advance_of(&bigfont_tile_480, '8') > 0);
    for (int i = 0; i < 8; i++) {
        const bigfont_t *f = all[i];
        CHECK(f->glyph_count > 0 && f->line_height > f->ascent);
        for (int g = 1; g < f->glyph_count; g++) CHECK(f->glyphs[g - 1].code < f->glyphs[g].code);
        CHECK(bigfont_text_width(f, "~") == 0);
        CHECK(bigfont_text_width(f, "\xE2\x82") == 0); /* truncated */
        if (i % 4 != 0) {
            CHECK(bigfont_text_width(f, "\xC2\xB0") > 0);
            CHECK(bigfont_text_width(f, "\xE2\x96\xBC") > 0);
            CHECK(bigfont_text_width(f, "\xE2\x96\xB2") > 0);
        }
    }

    /* blending bounds */
    const bigfont_t *f = &bigfont_tile_480;
    int lh = f->line_height;
    CHECK(lh <= H);
    bigfont_render(f, "8", 5, 0, 0xFFFF, buf, W, 0, lh);
    int white = 0, bad = 0;
    for (int i = 0; i < W * lh; i++) {
        if (buf[i] == 0xFFFF) white++;
        for (int s = 0; s < 3; s++) if (ch(buf[i], s) > (s == 1 ? 63 : 31)) bad++;
    }
    CHECK(white > 0);
    CHECK(bad == 0);
    /* mid-colour bg: channels stay between bg and fg */
    for (int i = 0; i < W * lh; i++) buf[i] = 0x8410;
    bigfont_render(f, "8", 5, 0, 0xF800, buf, W, 0, lh);
    for (int i = 0; i < W * lh; i++) {
        CHECK(ch(buf[i], 0) >= 16 && ch(buf[i], 0) <= 31);
        CHECK(ch(buf[i], 1) <= 32 && ch(buf[i], 2) <= 16);
    }

    /* strips equal a whole render */
    for (int i = 0; i < W * H; i++) a[i] = b[i] = 0x0000;
    bigfont_render(f, "8.5", 7, 0, 0xFFFF, a, W, 0, lh);
    bigfont_render(f, "8.5", 7, 0, 0xFFFF, b, W, 0, 10);
    bigfont_render(f, "8.5", 7, 0, 0xFFFF, b + W * 10, W, 10, lh - 10);
    CHECK(memcmp(a, b, sizeof(uint16_t) * W * lh) == 0);

    /* clipping: sentinels untouched */
    static uint16_t g[1 + W * 80 + 1];
    int xs[2] = {-10, W - 5};
    for (int k = 0; k < 2; k++) {
        for (unsigned i = 0; i < sizeof(g) / sizeof(g[0]); i++) g[i] = 0x1234;
        bigfont_render(f, "88", xs[k], 0, 0xFFFF, g + 1, W, 0, 80);
        CHECK(g[0] == 0x1234);
        CHECK(g[1 + W * 80] == 0x1234);
    }
    /* vertical clipping with negative line_y and rows beyond */
    for (unsigned i = 0; i < sizeof(g) / sizeof(g[0]); i++) g[i] = 0x1234;
    bigfont_render(f, "88", 3, -20, 0xFFFF, g + 1, W, 0, 80);
    bigfont_render(f, "88", 3, 60, 0xFFFF, g + 1, W, 0, 80);
    CHECK(g[0] == 0x1234 && g[1 + W * 80] == 0x1234);
    CHECK_DONE();
}
