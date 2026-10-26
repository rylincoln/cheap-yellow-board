#include "records.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "update_rules.h"

/* Layout of NVS namespace cyd_update:
     image_f, image_0, image_1  image_record_t blobs for factory (the launcher)
                                and the OTA slots
     floor_l, seen_l            the launcher's floor, and the commit time of
                                the last launcher release seen on GitHub
     app_name                   the app project that floor_a and seen_a are for
     floor_a, seen_a            the same two for that app */
#define RECORDS_NAMESPACE "cyd_update"
#define APP_NAME_KEY "app_name"
/* The launcher's own record of each OTA slot, which its menu shows. */
#define LAUNCHER_NAMESPACE "launcher"

static const char *TAG = "cyd_update";

typedef struct {
    uint8_t elf_sha256[32];
    int64_t commit_time;
    uint32_t source; /* image_source_t */
} image_record_t;

static const char *source_name(image_source_t source)
{
    switch (source) {
    case SOURCE_USB:
        return "USB";
    case SOURCE_MAC:
        return "the Mac";
    case SOURCE_GITHUB:
        return "GitHub";
    }
    return "?";
}

static const char *floor_key(image_kind_t kind)
{
    return kind == IMAGE_LAUNCHER ? "floor_l" : "floor_a";
}

static const char *seen_key(image_kind_t kind)
{
    return kind == IMAGE_LAUNCHER ? "seen_l" : "seen_a";
}

/* The OTA slot number of an app partition, or -1 for factory. */
static int slot_number(const esp_partition_t *partition)
{
    if (partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
        partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
        return partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
    }
    return -1;
}

/* Factory holds the launcher; the OTA slots hold apps. */
static image_kind_t kind_of(const esp_partition_t *partition)
{
    return slot_number(partition) < 0 ? IMAGE_LAUNCHER : IMAGE_APP;
}

static void record_key(const esp_partition_t *partition, char *key, size_t size)
{
    int slot = slot_number(partition);
    if (slot < 0) {
        snprintf(key, size, "image_f");
    } else {
        snprintf(key, size, "image_%d", slot);
    }
}

static int64_t read_time(const char *key)
{
    nvs_handle_t nvs;
    int64_t value = 0;
    if (nvs_open(RECORDS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_i64(nvs, key, &value) != ESP_OK) {
            value = 0;
        }
        nvs_close(nvs);
    }
    return value;
}

/* Writes only a changed value, so an hourly check doesn't wear the flash. */
static void write_time(nvs_handle_t nvs, const char *key, int64_t value)
{
    int64_t stored;
    if (nvs_get_i64(nvs, key, &stored) == ESP_OK && stored == value) {
        return;
    }
    esp_err_t result = nvs_set_i64(nvs, key, value);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Saving %s failed: %s", key, esp_err_to_name(result));
    }
}

static bool open_for_writing(const char *name, nvs_handle_t *nvs)
{
    esp_err_t result = nvs_open(name, NVS_READWRITE, nvs);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Opening NVS %s failed: %s", name, esp_err_to_name(result));
        return false;
    }
    return true;
}

static bool read_record(const esp_partition_t *partition, image_record_t *record)
{
    nvs_handle_t nvs;
    if (nvs_open(RECORDS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    char key[16];
    record_key(partition, key, sizeof(key));
    size_t size = sizeof(*record);
    bool found = nvs_get_blob(nvs, key, record, &size) == ESP_OK && size == sizeof(*record);
    nvs_close(nvs);
    return found;
}

/* Writes or, with name NULL, clears the launcher's name<n> and ver<n>, the
   same keys its menu reads and its own downloads write. */
static void write_launcher_slot(int slot, const char *name, const char *version)
{
    nvs_handle_t nvs;
    if (!open_for_writing(LAUNCHER_NAMESPACE, &nvs)) {
        return;
    }
    char name_key[16];
    char version_key[16];
    snprintf(name_key, sizeof(name_key), "name%d", slot);
    snprintf(version_key, sizeof(version_key), "ver%d", slot);
    if (name != NULL) {
        nvs_set_str(nvs, name_key, name);
        nvs_set_str(nvs, version_key, version);
    } else {
        nvs_erase_key(nvs, name_key);
        nvs_erase_key(nvs, version_key);
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

/* The app's floor and seen time belong to one project. When another project
   runs, or is installed, they start again for it: nothing seen yet, and the
   given floor. */
static void follow_app(const char *project, int64_t floor)
{
    nvs_handle_t nvs;
    if (!open_for_writing(RECORDS_NAMESPACE, &nvs)) {
        return;
    }
    char stored[32]; /* as long as image_desc_t's project */
    size_t size = sizeof(stored);
    if (nvs_get_str(nvs, APP_NAME_KEY, stored, &size) != ESP_OK || strcmp(stored, project) != 0) {
        nvs_set_str(nvs, APP_NAME_KEY, project);
        write_time(nvs, seen_key(IMAGE_APP), 0);
        write_time(nvs, floor_key(IMAGE_APP), floor);
        nvs_commit(nvs);
        ESP_LOGI(TAG, "App floor now for %s: %" PRId64, project, floor);
    }
    nvs_close(nvs);
}

/* Records partition's image and sets its kind's floor: a GitHub release's
   commit time, else the larger of commit_time and the last release seen. */
static void note(const esp_partition_t *partition, const image_desc_t *desc,
                 image_source_t source, int64_t commit_time)
{
    image_kind_t kind = kind_of(partition);
    int64_t floor = source == SOURCE_GITHUB
                        ? commit_time
                        : update_floor_after_local(commit_time, records_seen(kind));
    image_record_t record;
    memset(&record, 0, sizeof(record));
    memcpy(record.elf_sha256, desc->elf_sha256, sizeof(record.elf_sha256));
    record.commit_time = commit_time;
    record.source = source;

    nvs_handle_t nvs;
    if (!open_for_writing(RECORDS_NAMESPACE, &nvs)) {
        return;
    }
    char key[16];
    record_key(partition, key, sizeof(key));
    esp_err_t result = nvs_set_blob(nvs, key, &record, sizeof(record));
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Saving %s failed: %s", key, esp_err_to_name(result));
    }
    write_time(nvs, floor_key(kind), floor);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "%s in %s came from %s; %s floor %" PRId64, desc->project, partition->label,
             source_name(source), kind == IMAGE_LAUNCHER ? "launcher" : "app", floor);
}

int64_t records_floor(image_kind_t kind)
{
    return read_time(floor_key(kind));
}

int64_t records_seen(image_kind_t kind)
{
    return read_time(seen_key(kind));
}

void records_set_seen(image_kind_t kind, int64_t release_time)
{
    nvs_handle_t nvs;
    if (!open_for_writing(RECORDS_NAMESPACE, &nvs)) {
        return;
    }
    write_time(nvs, seen_key(kind), release_time);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void records_note_install(const esp_partition_t *partition, const image_desc_t *desc,
                          image_source_t source, int64_t commit_time, const char *launcher_version)
{
    if (kind_of(partition) == IMAGE_APP) {
        follow_app(desc->project, 0);
    }
    note(partition, desc, source, commit_time);
    int slot = slot_number(partition);
    if (slot >= 0) {
        write_launcher_slot(slot, desc->project, launcher_version != NULL ? launcher_version : "");
    }
}

void records_clear_slot(const esp_partition_t *partition)
{
    nvs_handle_t nvs;
    if (open_for_writing(RECORDS_NAMESPACE, &nvs)) {
        char key[16];
        record_key(partition, key, sizeof(key));
        nvs_erase_key(nvs, key);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    int slot = slot_number(partition);
    if (slot >= 0) {
        write_launcher_slot(slot, NULL, NULL);
    }
}

/* Reads the app description of the image in partition; false when there is none. */
static bool read_partition_desc(const esp_partition_t *partition, image_desc_t *desc)
{
    esp_app_desc_t app;
    if (esp_ota_get_partition_description(partition, &app) != ESP_OK) {
        return false;
    }
    /* The description's strings fill their fields without a terminator when full. */
    memset(desc, 0, sizeof(*desc));
    memcpy(desc->project, app.project_name, sizeof(desc->project) - 1);
    memcpy(desc->version, app.version, sizeof(desc->version) - 1);
    memcpy(desc->elf_sha256, app.app_elf_sha256, sizeof(desc->elf_sha256));
    return true;
}

/* Whether the whole image in partition checks out, as the bootloader checks it. */
static bool image_verifies(const esp_partition_t *partition)
{
    const esp_partition_pos_t position = {
        .offset = partition->address,
        .size = partition->size,
    };
    esp_image_metadata_t metadata;
    return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &position, &metadata) == ESP_OK;
}

/* An image whose ELF SHA-256 isn't the one recorded for its partition was
   put there some other way: over USB, or by the launcher's own menu. Its
   floor then comes from the commit time in its version. running says
   whether the partition holds the build that is running, which verified
   when it started. */
static void check_partition(const esp_partition_t *partition, bool running)
{
    image_desc_t desc;
    if (!read_partition_desc(partition, &desc)) {
        return;
    }
    image_record_t record;
    bool known = read_record(partition, &record) &&
                 memcmp(record.elf_sha256, desc.elf_sha256, sizeof(record.elf_sha256)) == 0;
    if (kind_of(partition) == IMAGE_APP) {
        follow_app(desc.project, known ? record.commit_time : 0);
    }
    if (known) {
        return;
    }
    /* An install cut off by a reset leaves a description that reads, ahead
       of an image that doesn't. Taking it for a USB flash would set the
       floor to the release being installed, which would then never be
       tried again. */
    if (!running && !image_verifies(partition)) {
        ESP_LOGW(TAG, "%s holds an incomplete launcher; its floor stays, so the next check "
                      "tries again",
                 partition->label);
        return;
    }
    note(partition, &desc, SOURCE_USB, update_version_commit_time(desc.version));
}

void records_check_usb(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        check_partition(running, true);
    }
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory != NULL && (running == NULL || factory->address != running->address)) {
        check_partition(factory, false);
    }
}
