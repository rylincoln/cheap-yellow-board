#include "install.h"

#include <string.h>

#include "esp_image_format.h"
#include "esp_log.h"
#include "records.h"

#define LAUNCHER_PROJECT "cyd_launcher"

static const char *TAG = "cyd_update";

/* Gives up on an install. A target that was erased loses its first sector
   too, so a part-written or refused image doesn't look like an app: the
   launcher's menu drops it and the bootloader won't start it. */
static void discard(install_t *install)
{
    if (install->ota_open) {
        esp_ota_abort(install->ota);
        install->ota_open = false;
    }
    mbedtls_md_free(&install->sha256);
    if (install->erased) {
        esp_err_t result =
            esp_partition_erase_range(install->partition, 0, install->partition->erase_size);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Clearing %s failed: %s", install->partition->label,
                     esp_err_to_name(result));
        }
        install->erased = false;
    }
    install->open = false;
}

/* True while the running build waits for its good start. */
static bool running_unconfirmed(const esp_partition_t *running)
{
    esp_ota_img_states_t state;
    return running != NULL && esp_ota_get_state_partition(running, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

esp_err_t install_begin(install_t *install, const image_desc_t *desc, size_t length,
                        install_source_t source)
{
    memset(install, 0, sizeof(*install));
    mbedtls_md_init(&install->sha256);
    install->desc = *desc;
    install->source = source;
    install->length = length;
    install->launcher = strcmp(desc->project, LAUNCHER_PROJECT) == 0;
    install->partition = install->launcher
                             ? esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL)
                             : esp_ota_get_next_update_partition(NULL);
    if (install->partition == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    /* A board laid out without a launcher runs its app from factory. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL && running->address == install->partition->address) {
        return ESP_ERR_OTA_PARTITION_CONFLICT;
    }
    if (length == 0 || length > install->partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    /* esp_ota_begin refuses this too, but only after the slot's records,
       which describe the build a rollback would return to, were cleared. */
    if (!install->launcher && running_unconfirmed(running)) {
        return ESP_ERR_OTA_ROLLBACK_INVALID_STATE;
    }
    if (mbedtls_md_setup(&install->sha256, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) != 0 ||
        mbedtls_md_starts(&install->sha256) != 0) {
        mbedtls_md_free(&install->sha256);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Installing %s %s into %s (%u bytes)", desc->project, desc->version,
             install->partition->label, (unsigned)length);
    records_clear_slot(install->partition);
    esp_err_t result;
    if (install->launcher) {
        size_t erase_size = install->partition->erase_size;
        install->erased = true;
        result = esp_partition_erase_range(install->partition, 0,
                                           (length + erase_size - 1) / erase_size * erase_size);
    } else {
        /* A refusal leaves the slot as it was, and an erase that fails has
           begun at the first sector, so only success counts as erased. */
        result = esp_ota_begin(install->partition, length, &install->ota);
        install->ota_open = result == ESP_OK;
        install->erased = result == ESP_OK;
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Erasing %s failed: %s", install->partition->label, esp_err_to_name(result));
        discard(install);
        return result;
    }
    install->open = true;
    return ESP_OK;
}

esp_err_t install_write(install_t *install, const void *data, size_t length)
{
    if (!install->open) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Only the announced length was erased. */
    if (length > install->length - install->written) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t result = install->launcher
                           ? esp_partition_write(install->partition, install->written, data, length)
                           : esp_ota_write(install->ota, data, length);
    if (result != ESP_OK) {
        return result;
    }
    if (mbedtls_md_update(&install->sha256, data, length) != 0) {
        return ESP_FAIL;
    }
    install->written += length;
    return ESP_OK;
}

/* Checks the image in factory the way the bootloader will at power-on. */
static esp_err_t verify_launcher(const esp_partition_t *partition)
{
    const esp_partition_pos_t position = {
        .offset = partition->address,
        .size = partition->size,
    };
    esp_image_metadata_t metadata;
    return esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) == ESP_OK
               ? ESP_OK
               : ESP_ERR_IMAGE_INVALID;
}

/* Ends the OTA write, which verifies the image, and boots the slot next. */
static esp_err_t finish_app(install_t *install)
{
    install->ota_open = false;
    esp_err_t result = esp_ota_end(install->ota);
    if (result == ESP_OK) {
        result = esp_ota_set_boot_partition(install->partition);
    }
    return result == ESP_ERR_OTA_VALIDATE_FAILED ? ESP_ERR_IMAGE_INVALID : result;
}

esp_err_t install_finish(install_t *install, const uint8_t *expected_sha256, int64_t commit_time,
                         const char *launcher_version)
{
    if (!install->open) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    uint8_t sha256[32];
    if (install->written != install->length) {
        ESP_LOGE(TAG, "Image ended at %u of %u bytes", (unsigned)install->written,
                 (unsigned)install->length);
        result = ESP_ERR_INVALID_SIZE;
    } else if (mbedtls_md_finish(&install->sha256, sha256) != 0) {
        result = ESP_FAIL;
    } else if (expected_sha256 != NULL && memcmp(sha256, expected_sha256, sizeof(sha256)) != 0) {
        ESP_LOGE(TAG, "Image hash differs");
        result = ESP_ERR_INVALID_CRC;
    } else {
        result = install->launcher ? verify_launcher(install->partition) : finish_app(install);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Install of %s failed: %s", install->desc.project, esp_err_to_name(result));
        discard(install);
        return result;
    }
    /* The floor moves only once the image is in place and, for an app, set
       to boot next, so an install that failed is tried again. */
    records_note_install(install->partition, &install->desc,
                         install->source == INSTALL_FROM_MAC ? SOURCE_MAC : SOURCE_GITHUB,
                         commit_time, launcher_version);
    mbedtls_md_free(&install->sha256);
    install->open = false;
    install->erased = false;
    ESP_LOGI(TAG, "Installed %s %s into %s", install->desc.project, install->desc.version,
             install->partition->label);
    return ESP_OK;
}

void install_abort(install_t *install)
{
    if (install->open) {
        ESP_LOGW(TAG, "Install of %s abandoned", install->desc.project);
        discard(install);
    }
}

bool install_is_launcher(const install_t *install)
{
    return install->launcher;
}

int install_percent(const install_t *install)
{
    if (install->length == 0) {
        return 0;
    }
    return (int)((uint64_t)install->written * 100 / install->length);
}
