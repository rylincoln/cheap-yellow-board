#include "check.h"
#include "bigfont.h"
#include "bigfont_data.h"

#define W 200
#define H 160
#define WN 60 /* narrow window width */
#define OFF 100 /* reference margin, columns */
#define RO 100 /* reference margin, rows */
#define WR (WN + 2 * OFF)

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

    /* ink rows of the tile '8' */
    int yo = 0, gh = 0;
    for (int i = 0; i < f->glyph_count; i++)
        if (f->glyphs[i].code == '8') { yo = f->glyphs[i].y_offset; gh = f->glyphs[i].height; }
    CHECK(gh > 20 && yo + gh <= lh);

    /* strips split through the middle of the ink equal a whole render */
    int mid = yo + gh / 2;
    for (int i = 0; i < W * H; i++) a[i] = b[i] = 0x0000;
    bigfont_render(f, "8.5", 7, 0, 0xFFFF, a, W, 0, lh);
    bigfont_render(f, "8.5", 7, 0, 0xFFFF, b, W, 0, mid);
    bigfont_render(f, "8.5", 7, 0, 0xFFFF, b + W * mid, W, mid, lh - mid);
    CHECK(memcmp(a, b, sizeof(uint16_t) * W * lh) == 0);
    int inked = 0;
    for (int i = 0; i < W * mid; i++) inked += b[i] != 0;
    CHECK(inked > 0); /* the first strip really holds ink */

    /* windows that clip at every edge match a roomy reference render */
    static const int xs[] = {-10, 0, 20, WN - 5, WN - 30, WN};
    int lys[3] = {-yo, -yo - 10, -yo + 5};
    int rowsv[3] = {gh, gh, gh - 8};
    for (int li = 0; li < 3; li++) {
        for (unsigned xi = 0; xi < sizeof(xs) / sizeof(xs[0]); xi++) {
            int x = xs[xi], rows = rowsv[li];
            static uint16_t narrow[1 + WN * 80 + 1], ref[WR * (80 + 2 * RO)];
            for (unsigned i = 0; i < sizeof(narrow) / sizeof(narrow[0]); i++) narrow[i] = 0x1234;
            for (unsigned i = 0; i < sizeof(ref) / sizeof(ref[0]); i++) ref[i] = 0x1234;
            bigfont_render(f, "88", x, lys[li], 0xFFFF, narrow + 1, WN, 0, rows);
            bigfont_render(f, "88", x + OFF, lys[li], 0xFFFF, ref, WR, -RO, rows + 2 * RO);
            CHECK(narrow[0] == 0x1234);
            CHECK(narrow[1 + WN * rows] == 0x1234);
            int diff = 0;
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < WN; c++)
                    diff += narrow[1 + r * WN + c] != ref[(r + RO) * WR + c + OFF];
            CHECK(diff == 0);
            if (li == 0 && x == 20) { /* ink reaches the first and last rows */
                int top = 0, bottom = 0;
                for (int c = 0; c < WN; c++) {
                    top += narrow[1 + c] != 0x1234;
                    bottom += narrow[1 + (rows - 1) * WN + c] != 0x1234;
                }
                CHECK(top > 0 && bottom > 0);
            }
        }
    }
    CHECK_DONE();
}
