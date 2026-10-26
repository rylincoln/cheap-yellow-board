#ifndef IMAGE_DESC_H
#define IMAGE_DESC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IMAGE_DESC_BYTES 256

typedef struct {
    char project[32];
    char version[32];
    uint8_t elf_sha256[32];
} image_desc_t;

/* Byte 0 is 0xE9; 0xABCD5432 (little-endian) at 32; version at 48; project at 80, NUL-terminated
   and not empty; ELF SHA-256 at 176. */
bool image_desc_read(const uint8_t *image, size_t length, image_desc_t *desc);

#endif
