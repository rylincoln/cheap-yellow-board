#include "check.h"
#include "glance_tile.h"

int main(void)
{
    rect_t r = glance_scale_rect((rect_t){4, 244, 472, 72}, 320, 240);
    CHECK(r.x == 3 && r.y == 183 && r.w == 315 && r.h == 54);

    glance_tile_t tile = {0};
    for (int i = 0; i < 9; i++) glance_add_text(&tile, NULL, "x", i, i, 0);
    CHECK(tile.count == 8);
    CHECK(tile.texts[7].x == 7);

    CHECK(glance_rgb(0xFF, 0x8A, 0x7A) == 0xFC4F);
    CHECK_DONE();
}
