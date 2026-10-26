/* New releases from GitHub, installed without a tap.

   At network-up, then every hour, the board asks GitHub for the release
   tagged launcher and the one tagged with the running app's project name.
   A release whose notes carry a commit time above its image's floor is
   installed once the app is on a screen an update may take: the launcher
   into factory, where it runs at the next power-on, and the app into the
   other slot, which it then restarts into. Anything that goes wrong before
   the screen is lent only goes to the log; after that it shows UPDATE
   FAILED. Either way the floor stays, so the next check tries again. */

#include "github_check.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cyd_display.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "image_desc.h"
#include "install.h"
#include "push_auth.h"
#include "records.h"
#include "update_internal.h"
#include "update_rules.h"

#define CHECK_STACK 8192
#define CHECK_INTERVAL_TICKS ((TickType_t)60 * 60 * configTICK_RATE_HZ)
/* A reconnect wakes the check early, but a network that keeps dropping
   still checks at most this often: GitHub allows 60 unauthenticated
   requests an hour for the whole household's address. */
#define CHECK_SPACING_US (5LL * 60 * 1000 * 1000)
/* While a release waits for the app's readings screen and the lock. */
#define TURN_POLL_MS 5000
#define SCREEN_WAIT_MS 5000
#define JSON_LIMIT (16 * 1024)
#define CHUNK_BYTES 4096
#define MAX_REDIRECTS 5
/* The timeouts and buffers of the launcher's own GitHub requests. */
#define API_TIMEOUT_MS 10000
#define DOWNLOAD_TIMEOUT_MS 15000
#define LAUNCHER_TAG "launcher"
#define RELEASE_URL_FORMAT "https://api.github.com/repos/" CYD_IMAGE_REPO "/releases/tags/%s"
#define URL_SIZE 128
#define PROJECT_SIZE 33
#define SOURCE "GitHub"

static const char *TAG = "cyd_update";

static TaskHandle_t check_task;

/* Opens the client's URL, following up to MAX_REDIRECTS redirects as the
   launcher's downloads do: GitHub serves assets from another host. ESP_OK
   once the reply is 200, with *length its Content-Length or 0 when it gives
   none. *status is the last HTTP status, or -1 when there was none. */
static esp_err_t open_following_redirects(esp_http_client_handle_t client, int *status,
                                          int64_t *length)
{
    *status = -1;
    for (int redirects = 0; redirects <= MAX_REDIRECTS; ++redirects) {
        esp_err_t result = esp_http_client_open(client, 0);
        if (result != ESP_OK) {
            return result;
        }
        *length = esp_http_client_fetch_headers(client);
        if (*length < 0) {
            return ESP_ERR_HTTP_FETCH_HEADER;
        }
        *status = esp_http_client_get_status_code(client);
        if (*status == 200) {
            return ESP_OK;
        }
        if (*status < 300 || *status >= 400) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        esp_http_client_flush_response(client, NULL);
        result = esp_http_client_set_redirection(client);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_ERR_HTTP_MAX_REDIRECT;
}

/* Reads up to length bytes of the body; fewer only where it ends. -1 when
   the connection fails or times out. */
static int read_body(esp_http_client_handle_t client, uint8_t *buffer, size_t length)
{
    size_t got = 0;
    while (got < length) {
        int bytes = esp_http_client_read(client, (char *)buffer + got, (int)(length - got));
        if (bytes < 0) {
            return -1;
        }
        if (bytes == 0) {
            break;
        }
        got += (size_t)bytes;
    }
    return (int)got;
}

/* Reads the whole reply into a new buffer of at most JSON_LIMIT bytes,
   which the caller frees. */
static esp_err_t read_reply(esp_http_client_handle_t client, int64_t content_length, char **json,
                            size_t *length)
{
    if (content_length > JSON_LIMIT) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t capacity = content_length > 0 ? (size_t)content_length : JSON_LIMIT;
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    int got = read_body(client, (uint8_t *)buffer, capacity);
    uint8_t extra;
    esp_err_t result = ESP_OK;
    if (got < 0 || (content_length > 0 && (size_t)got != capacity)) {
        result = ESP_ERR_HTTP_INCOMPLETE_DATA;
    } else if ((size_t)got == capacity && read_body(client, &extra, 1) != 0) {
        result = ESP_ERR_INVALID_SIZE;
    }
    if (result != ESP_OK) {
        free(buffer);
        return result;
    }
    *json = buffer;
    *length = (size_t)got;
    return ESP_OK;
}

/* Reads GitHub's release with this tag. False, and logged, when there is
   none, or it can't be read, or it has no firmware.bin. The reply and the
   connection are gone by the time it returns. */
static bool fetch_release(const char *tag, update_release_t *release)
{
    char url[URL_SIZE];
    snprintf(url, sizeof(url), RELEASE_URL_FORMAT, tag);
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = API_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGW(TAG, "No memory to ask GitHub for release %s", tag);
        return false;
    }
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    int status;
    int64_t content_length = 0;
    char *json = NULL;
    size_t length = 0;
    esp_err_t result = open_following_redirects(client, &status, &content_length);
    if (result == ESP_OK) {
        result = read_reply(client, content_length, &json, &length);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    bool parsed = result == ESP_OK && update_parse_release(json, length, release);
    free(json);
    if (status == 404) {
        ESP_LOGW(TAG, "GitHub has no release tagged %s", tag);
    } else if (result != ESP_OK) {
        ESP_LOGW(TAG, "Asking GitHub for release %s failed: %s, HTTP %d", tag,
                 esp_err_to_name(result), status);
    } else if (!parsed) {
        ESP_LOGW(TAG, "Release %s has no firmware.bin", tag);
    }
    return parsed;
}

/* Where an image of this kind installs, as install_begin chooses it. */
static const esp_partition_t *target_partition(image_kind_t kind)
{
    return kind == IMAGE_LAUNCHER
               ? esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                          ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL)
               : esp_ota_get_next_update_partition(NULL);
}

/* Waits, looking every TURN_POLL_MS, until the app is on a screen an update
   may take and no other update runs, then borrows the screen. True with the
   lock held and the screen lent. False, holding neither, once the release
   is no longer above the floor: a push from the Mac came first. */
static bool wait_for_turn(image_kind_t kind, const char *tag, int64_t release_time)
{
    bool told = false;
    while (true) {
        if (update_app_interruptible() && update_lock(NULL)) {
            if (!update_release_wanted(release_time, records_floor(kind))) {
                update_unlock();
                return false;
            }
            if (update_screen_take(SCREEN_WAIT_MS)) {
                /* A tap can start a bird lookup between the first look and
                   the app lending the screen: the screen goes straight back. */
                if (update_app_interruptible()) {
                    return true;
                }
                ESP_LOGI(TAG, "The app left its readings before lending the screen");
            }
            update_unlock();
        }
        if (!told) {
            ESP_LOGI(TAG, "Release %s waits for the readings screen and the update lock", tag);
            told = true;
        }
        vTaskDelay(pdMS_TO_TICKS(TURN_POLL_MS));
    }
}

/* Streams the release's firmware.bin into its partition through install,
   CHUNK_BYTES at a time. NULL once every byte is written; else the reason
   UPDATE FAILED shows. */
static const char *stream_asset(esp_http_client_handle_t client, const update_release_t *release,
                                const char *project, const char *label, uint8_t *chunk,
                                install_t *install)
{
    int status;
    int64_t length = 0;
    esp_err_t result = open_following_redirects(client, &status, &length);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Downloading %s failed: %s, HTTP %d", project, esp_err_to_name(result),
                 status);
        return "download failed";
    }
    if (length > 0 && length != (int64_t)release->size) {
        ESP_LOGW(TAG, "GitHub sent %" PRId64 " bytes of a %" PRIu32 "-byte firmware.bin", length,
                 release->size);
        return "size differs";
    }
    /* Fewer bytes than both the description and the release's size means
       the connection ended early, not that the image is wrong. */
    int got = read_body(client, chunk, IMAGE_DESC_BYTES);
    if (got < 0 || ((size_t)got < IMAGE_DESC_BYTES && (size_t)got < release->size)) {
        ESP_LOGW(TAG, "Download of %s stopped before its app description", project);
        return "transfer stopped";
    }
    image_desc_t desc;
    if (!image_desc_read(chunk, (size_t)got, &desc)) {
        return "not an app image";
    }
    if (strcmp(desc.project, project) != 0) {
        ESP_LOGW(TAG, "The release for %s holds %s", project, desc.project);
        return "wrong project";
    }
    result = install_begin(install, &desc, release->size, INSTALL_FROM_GITHUB);
    if (result == ESP_ERR_INVALID_SIZE) {
        return "too big";
    }
    if (result != ESP_OK) {
        return "flash error";
    }
    size_t received = (size_t)got;
    result = install_write(install, chunk, (size_t)got);
    while (result == ESP_OK && received < release->size) {
        update_screen_progress(label, SOURCE, install_percent(install));
        size_t left = release->size - received;
        got = read_body(client, chunk, left < CHUNK_BYTES ? left : CHUNK_BYTES);
        if (got <= 0) {
            ESP_LOGW(TAG, "Download of %s stopped at %u of %" PRIu32 " bytes", project,
                     (unsigned)received, release->size);
            return "transfer stopped";
        }
        result = install_write(install, chunk, (size_t)got);
        received += (size_t)got;
    }
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Writing %s failed: %s", project, esp_err_to_name(result));
        return "flash error";
    }
    update_screen_progress(label, SOURCE, install_percent(install));
    return NULL;
}

/* Downloads and checks the release, with the lock held and the screen lent,
   and shows how it ended. A new app restarts the board; otherwise the
   screen and the lock go back. */
static void install_release(const update_release_t *release, const char *project,
                            const uint8_t *expected_sha256)
{
    bool launcher = strcmp(project, LAUNCHER_PROJECT) == 0;
    const char *label = launcher ? UPDATE_LAUNCHER_LABEL : update_app_label();
    update_screen_progress(label, SOURCE, 0);

    install_t install;
    memset(&install, 0, sizeof(install)); /* install_abort is safe before install_begin */
    const char *reason = "no memory";
    esp_http_client_config_t config = {
        .url = release->url,
        .timeout_ms = DOWNLOAD_TIMEOUT_MS,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    uint8_t *chunk = malloc(CHUNK_BYTES);
    if (client != NULL && chunk != NULL) {
        reason = stream_asset(client, release, project, label, chunk, &install);
    }
    free(chunk);
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (reason == NULL) {
        esp_err_t result = install_finish(&install, expected_sha256, release->commit_time,
                                          release->updated_at);
        reason = result == ESP_OK                  ? NULL
                 : result == ESP_ERR_INVALID_CRC   ? "hash differs"
                 : result == ESP_ERR_IMAGE_INVALID ? "does not verify"
                                                   : "flash error";
    }

    if (reason != NULL) {
        install_abort(&install);
        ESP_LOGW(TAG, "Install of %s from GitHub failed: %s", project, reason);
        update_screen_finish(UPDATE_TITLE_FAILED, reason, UPDATE_FAILED_MS, false);
    } else if (launcher) {
        update_screen_finish(UPDATE_TITLE_LAUNCHER, UPDATE_DETAIL_LAUNCHER, UPDATE_LAUNCHER_MS,
                             false);
    } else {
        update_screen_finish(UPDATE_TITLE_RESTARTING, NULL, UPDATE_RESTARTING_MS, true);
        esp_restart();
    }
    update_unlock();
}

/* Installs the release tagged tag when it is wanted. project is the image
   it must hold. An app that installs restarts the board instead of
   returning. */
static void check_image(image_kind_t kind, const char *tag, const char *project)
{
    update_release_t release;
    if (!fetch_release(tag, &release)) {
        return;
    }
    /* Notes without a commit time say nothing about which build this is. */
    if (release.commit_time > 0) {
        records_set_seen(kind, release.commit_time);
    }
    int64_t floor = records_floor(kind);
    if (!update_release_wanted(release.commit_time, floor)) {
        ESP_LOGI(TAG, "Release %s: commit time %" PRId64 ", floor %" PRId64 "; nothing to install",
                 tag, release.commit_time, floor);
        return;
    }
    if (release.size == 0 || strncmp(release.url, "https://", 8) != 0) {
        ESP_LOGW(TAG, "Release %s: firmware.bin has no size or no HTTPS address yet", tag);
        return;
    }
    const esp_partition_t *target = target_partition(kind);
    if (target == NULL || release.size > target->size) {
        ESP_LOGW(TAG, "Release %s: firmware.bin is %" PRIu32 " bytes; %s holds %" PRIu32, tag,
                 release.size, target != NULL ? target->label : "no partition",
                 target != NULL ? target->size : 0);
        return;
    }
    uint8_t digest[32];
    const uint8_t *expected_sha256 = NULL;
    if (release.sha256[0] != '\0') {
        if (!push_auth_parse_hex32(release.sha256, digest)) {
            ESP_LOGW(TAG, "Release %s: firmware.bin's digest isn't hex", tag);
            return;
        }
        expected_sha256 = digest;
    }
    ESP_LOGI(TAG, "Release %s: commit time %" PRId64 " is above the floor %" PRId64
                  "; installing %" PRIu32 " bytes",
             tag, release.commit_time, floor, release.size);
    if (!wait_for_turn(kind, tag, release.commit_time)) {
        ESP_LOGI(TAG, "Release %s is no longer above its floor; not installing it", tag);
        return;
    }
    install_release(&release, project, expected_sha256);
}

/* A board laid out without a launcher runs its app from factory, where a
   launcher release could never install. */
static bool running_from_factory(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *factory = target_partition(IMAGE_LAUNCHER);
    return running != NULL && factory != NULL && running->address == factory->address;
}

static void check_releases(void)
{
    /* The description's 32-character name has no terminator when full. */
    char project[PROJECT_SIZE];
    snprintf(project, sizeof(project), "%.32s", esp_app_get_description()->project_name);
    if (running_from_factory()) {
        ESP_LOGI(TAG, "Checking GitHub for %s; it runs from factory, so not for the launcher",
                 project);
    } else {
        ESP_LOGI(TAG, "Checking GitHub for the launcher and %s", project);
        check_image(IMAGE_LAUNCHER, LAUNCHER_TAG, LAUNCHER_PROJECT);
    }
    check_image(IMAGE_APP, project, project);
    /* For watching memory on the board: every reply and connection is gone by now. */
    ESP_LOGI(TAG, "GitHub check done; free heap %" PRIu32 " bytes, lowest %" PRIu32
                  ", largest block %u; %u bytes of stack never used",
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void check_task_main(void *argument)
{
    (void)argument;
    while (true) {
        int64_t started_us = esp_timer_get_time();
        check_releases();
        ulTaskNotifyTake(pdTRUE, CHECK_INTERVAL_TICKS);
        int64_t spacing_left_us = started_us + CHECK_SPACING_US - esp_timer_get_time();
        if (spacing_left_us > 0) {
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(spacing_left_us / 1000)));
        }
    }
}

esp_err_t github_check_start(void)
{
    if (check_task != NULL) {
        xTaskNotifyGive(check_task);
        return ESP_OK;
    }
    if (xTaskCreate(check_task_main, "github_check", CHECK_STACK, NULL, 1, &check_task) !=
        pdPASS) {
        check_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
