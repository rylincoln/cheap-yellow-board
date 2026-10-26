/* Writes an image where it belongs, hashing it on the way, and checks it
   before it can start: the launcher to the factory partition, an app to the
   OTA slot that isn't running. */
#ifndef INSTALL_H
#define INSTALL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "image_desc.h"
#include "mbedtls/md.h"

typedef enum { INSTALL_FROM_MAC, INSTALL_FROM_GITHUB } install_source_t;

typedef struct install {
    const esp_partition_t *partition; /* the target; set by install_begin even when it fails */
    image_desc_t desc;
    install_source_t source;
    bool launcher;          /* factory, written with plain partition calls */
    bool open;              /* begun, and not yet finished or aborted */
    bool erased;            /* the target's old contents are gone */
    bool ota_open;          /* ota holds an esp_ota_begin handle */
    esp_ota_handle_t ota;
    mbedtls_md_context_t sha256;
    size_t length;          /* the whole image, as announced */
    size_t written;
} install_t; /* callers keep one on their stack */

/* project cyd_launcher: the factory partition, with plain erase and write; else the OTA slot that
   isn't running, through esp_ota_begin. ESP_ERR_INVALID_SIZE when it doesn't fit, before touching
   flash. Clears that slot's records, then erases. */
esp_err_t install_begin(install_t *install, const image_desc_t *desc, size_t length, install_source_t source);
esp_err_t install_write(install_t *install, const void *data, size_t length); /* also hashes */
/* expected may be NULL (a GitHub asset without a digest). It checks the hash, verifies the image
   (esp_ota_end, or esp_image_verify for factory), records it, and makes an app the boot slot.
   ESP_ERR_INVALID_CRC: the hash differs; ESP_ERR_IMAGE_INVALID: the image doesn't verify.
   On any failure the install is already abandoned. */
esp_err_t install_finish(install_t *install, const uint8_t *expected_sha256, int64_t commit_time,
                         const char *launcher_version);
void install_abort(install_t *install); /* any time after install_begin, whatever it returned */
bool install_is_launcher(const install_t *install);
int install_percent(const install_t *install);

#endif
