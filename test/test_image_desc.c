#include "check.h"
#include "image_desc.h"

static void build(uint8_t *image)
{
    memset(image, 0, IMAGE_DESC_BYTES);
    image[0] = 0xE9;
    image[32] = 0x32;
    image[33] = 0x54;
    image[34] = 0xCD;
    image[35] = 0xAB;
    strcpy((char *)image + 48, "1793027864-1dec0ea");
    strcpy((char *)image + 80, "home_dashboard");
    for (int i = 0; i < 32; ++i) {
        image[176 + i] = (uint8_t)i;
    }
}

int main(void)
{
    uint8_t image[IMAGE_DESC_BYTES];
    image_desc_t desc;
    build(image);
    CHECK(image_desc_read(image, sizeof(image), &desc));
    CHECK_STR(desc.project, "home_dashboard");
    CHECK_STR(desc.version, "1793027864-1dec0ea");
    CHECK(desc.elf_sha256[0] == 0 && desc.elf_sha256[31] == 31);
    CHECK(!image_desc_read(image, 255, &desc));

    build(image);
    image[0] = 0x00;
    CHECK(!image_desc_read(image, sizeof(image), &desc));

    build(image);
    image[0] = 0xAA;
    image[1] = 0x50;
    CHECK(!image_desc_read(image, sizeof(image), &desc));

    build(image);
    memset(image + 32, 0, 4);
    CHECK(!image_desc_read(image, sizeof(image), &desc));

    build(image);
    memset(image + 80, 'x', 32);
    CHECK(!image_desc_read(image, sizeof(image), &desc));

    build(image);
    image[80] = 0;
    CHECK(!image_desc_read(image, sizeof(image), &desc));
    CHECK_DONE();
}
