#include "image_desc.h"

#include <string.h>

#define IMAGE_MAGIC_BYTE 0xE9
#define DESC_MAGIC_OFFSET 32
#define DESC_VERSION_OFFSET 48
#define DESC_PROJECT_OFFSET 80
#define DESC_SHA256_OFFSET 176

/* Reads the app description from an image's first 256 bytes, so a wrong file
   (a partition table, a bootloader) is refused before anything is erased. */
bool image_desc_read(const uint8_t *image, size_t length, image_desc_t *desc)
{
    static const uint8_t desc_magic[4] = {0x32, 0x54, 0xCD, 0xAB};
    if (length < IMAGE_DESC_BYTES || image[0] != IMAGE_MAGIC_BYTE ||
        memcmp(image + DESC_MAGIC_OFFSET, desc_magic, sizeof(desc_magic)) != 0) {
        return false;
    }
    if (image[DESC_PROJECT_OFFSET] == '\0' ||
        memchr(image + DESC_PROJECT_OFFSET, '\0', sizeof(desc->project)) == NULL) {
        return false;
    }
    memcpy(desc->project, image + DESC_PROJECT_OFFSET, sizeof(desc->project));
    memcpy(desc->version, image + DESC_VERSION_OFFSET, sizeof(desc->version));
    desc->version[sizeof(desc->version) - 1] = '\0';
    memcpy(desc->elf_sha256, image + DESC_SHA256_OFFSET, sizeof(desc->elf_sha256));
    return true;
}
