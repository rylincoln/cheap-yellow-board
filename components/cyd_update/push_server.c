/* Builds pushed from the Mac by tools/cyd_push.py, and the board's mDNS name.

   The Mac asks GET /update/challenge for a one-time challenge, then sends
   POST /update with the image as the body and an HMAC-SHA256, keyed with the
   update key, over the challenge and what it says about the image. Nothing
   is erased before that HMAC and the image's description check out. Every
   reply is one line of plain text, which the Mac prints. */

#include "push_server.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cyd_display.h"
#include "esp_http_server.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "image_desc.h"
#include "install.h"
#include "mbedtls/platform_util.h"
#include "mdns.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "push_auth.h"
#include "update_internal.h"

#define SERVER_PORT 80
#define SERVER_STACK 8192
/* A push is one connection at a time; the rest of lwIP's 10 sockets stay
   with the app's own requests. The oldest connection makes way for a new one. */
#define SERVER_SOCKETS 3
#define CHUNK_BYTES 4096
/* httpd_req_recv waits 5 s at a time: a push silent for 30 s has stopped. */
#define RECEIVE_TRIES 6
/* How long a pushed app waits for the screen to show RESTARTING. */
#define RESTART_SCREEN_WAIT_MS 10000
#define PUSHED_VERSION "pushed over LAN"

#define HEADER_CHALLENGE "X-Cyd-Challenge"
#define HEADER_PROJECT "X-Cyd-Project"
#define HEADER_SHA256 "X-Cyd-Sha256"
#define HEADER_COMMIT_TIME "X-Cyd-Commit-Time"
#define HEADER_HMAC "X-Cyd-Hmac"

#define STATUS_FORBIDDEN "403 Forbidden"
#define STATUS_CONFLICT "409 Conflict"
#define STATUS_UNPROCESSABLE "422 Unprocessable Entity"

#define REPLY_NO_KEY "no update key on the board: set one on the phone setup page"
#define REPLY_WRONG_KEY "wrong key or expired challenge"

/* The stored key is 20 characters; the launcher saves it normalized. */
#define KEY_SIZE 40
/* Header buffers hold one character more than a valid value: see get_header. */
#define PROJECT_SIZE 33 /* an image's project name is at most 31 characters */
#define HEX32_SIZE 66   /* 64 hex digits */
#define COMMIT_TIME_DIGITS 18
#define MESSAGE_SIZE 192
#define REPLY_SIZE 80

static const char *TAG = "cyd_update";

static httpd_handle_t server;
static bool mdns_running;
/* The server runs one request at a time on its own task, so only that
   task touches the challenge. */
static push_challenge_t challenge;

/* A push's Content-Length and X-Cyd headers, checked for shape only. */
typedef struct {
    uint32_t length;
    char challenge[PUSH_CHALLENGE_HEX_LENGTH + 2];
    char project[PROJECT_SIZE];
    char sha256_hex[HEX32_SIZE]; /* as sent, since the HMAC covers the text */
    uint8_t sha256[32];
    uint8_t hmac[32];
    int64_t commit_time;
} push_t;

/* How a push that got the lock failed. */
typedef struct {
    const char *status; /* NULL when the Mac stopped sending: nobody is left to answer */
    char text[REPLY_SIZE];
    const char *reason; /* row 2 of UPDATE FAILED */
    bool screen_asked;
} failure_t;

static esp_err_t reply(httpd_req_t *request, const char *status, const char *text)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, text);
}

/* A push turned away before it got the lock. */
static esp_err_t refuse(httpd_req_t *request, const char *status, const char *text)
{
    ESP_LOGW(TAG, "Push refused: %s", text);
    return reply(request, status, text);
}

/* Read for each request, so the key is in memory only while one needs it.
   False when none is saved. */
static bool read_key(char key[KEY_SIZE])
{
    key[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(CYD_CONFIG_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t size = KEY_SIZE;
    esp_err_t result = nvs_get_str(nvs, CYD_CONFIG_UPDATE_KEY, key, &size);
    nvs_close(nvs);
    return result == ESP_OK && key[0] != '\0';
}

/* Copies a header that is present, not empty, and at most size - 2
   characters. ESP-IDF quietly cuts a value of exactly size characters
   short, so the spare character is how a value that is too long shows. */
static bool get_header(httpd_req_t *request, const char *name, char *value, size_t size)
{
    return httpd_req_get_hdr_value_str(request, name, value, size) == ESP_OK &&
           value[0] != '\0' && strlen(value) < size - 1;
}

/* Decimal digits only, as the Mac writes the commit time. */
static bool parse_commit_time(const char *text, int64_t *value)
{
    int64_t parsed = 0;
    for (const char *digit = text; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
        parsed = parsed * 10 + (*digit - '0');
    }
    *value = parsed;
    return true;
}

static bool read_headers(httpd_req_t *request, push_t *push)
{
    char commit_time[COMMIT_TIME_DIGITS + 2];
    char hmac[HEX32_SIZE];
    push->length = (uint32_t)request->content_len;
    return request->content_len > 0 &&
           get_header(request, HEADER_CHALLENGE, push->challenge, sizeof(push->challenge)) &&
           strlen(push->challenge) == PUSH_CHALLENGE_HEX_LENGTH &&
           get_header(request, HEADER_PROJECT, push->project, sizeof(push->project)) &&
           get_header(request, HEADER_SHA256, push->sha256_hex, sizeof(push->sha256_hex)) &&
           push_auth_parse_hex32(push->sha256_hex, push->sha256) &&
           get_header(request, HEADER_COMMIT_TIME, commit_time, sizeof(commit_time)) &&
           parse_commit_time(commit_time, &push->commit_time) &&
           get_header(request, HEADER_HMAC, hmac, sizeof(hmac)) &&
           push_auth_parse_hex32(hmac, push->hmac);
}

/* HMAC-SHA256 of the message, keyed with the key's characters. PSA holds
   its own copy of the key for this one MAC, and destroying it wipes that. */
static bool compute_hmac(const char *key, const char *message, size_t length, uint8_t mac[32])
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    size_t mac_length = 0;
    psa_status_t status =
        psa_import_key(&attributes, (const uint8_t *)key, strlen(key), &key_id);
    if (status == PSA_SUCCESS) {
        status = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256), (const uint8_t *)message,
                                 length, mac, 32, &mac_length);
    }
    psa_destroy_key(key_id); /* does nothing when the import failed */
    return status == PSA_SUCCESS && mac_length == 32;
}

/* Whether the Mac's HMAC is the one the key makes of this push. */
static bool hmac_matches(const push_t *push, const char *key)
{
    char message[MESSAGE_SIZE];
    size_t length = push_auth_message(message, sizeof(message), push->challenge, push->length,
                                      push->sha256_hex, push->project, push->commit_time);
    uint8_t expected[32];
    bool matches = length > 0 && compute_hmac(key, message, length, expected) &&
                   push_auth_equal(expected, push->hmac);
    mbedtls_platform_zeroize(expected, sizeof(expected));
    return matches;
}

/* Reads exactly length bytes of the body. False when the Mac stops sending:
   the connection closed, or nothing came for RECEIVE_TRIES waits in a row. */
static bool receive(httpd_req_t *request, uint8_t *buffer, size_t length)
{
    size_t received = 0;
    int timeouts = 0;
    while (received < length) {
        int got = httpd_req_recv(request, (char *)buffer + received, length - received);
        if (got == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < RECEIVE_TRIES) {
            continue;
        }
        if (got <= 0) {
            return false;
        }
        timeouts = 0;
        received += (size_t)got;
    }
    return true;
}

/* Notes the reply and the screen's reason for a failed push; returns false. */
static bool fail(failure_t *failure, const char *status, const char *reason,
                 const char *format, ...) __attribute__((format(printf, 4, 5)));
static bool fail(failure_t *failure, const char *status, const char *reason,
                 const char *format, ...)
{
    failure->status = status;
    failure->reason = reason;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(failure->text, sizeof(failure->text), format, arguments);
    va_end(arguments);
    return false;
}

/* The app lends the screen when it gets to the request, so each step looks
   again and shows the percentage once it has. */
static void show_progress(const char *label, const install_t *install)
{
    if (update_screen_take(0)) {
        update_screen_progress(label, "Mac", install_percent(install));
    }
}

/* The checks from the challenge on, in the protocol's order, then the
   transfer. True once the image is installed; else failure says why. */
static bool receive_and_install(httpd_req_t *request, const push_t *push,
                                push_challenge_t *offered, const char *key, uint8_t *chunk,
                                install_t *install, failure_t *failure)
{
    if (!push_challenge_take(offered, push->challenge, esp_timer_get_time()) ||
        !hmac_matches(push, key)) {
        return fail(failure, STATUS_FORBIDDEN, NULL, REPLY_WRONG_KEY);
    }
    size_t head = push->length < IMAGE_DESC_BYTES ? push->length : IMAGE_DESC_BYTES;
    if (!receive(request, chunk, head)) {
        return fail(failure, NULL, NULL, "transfer stopped");
    }
    image_desc_t desc;
    if (!image_desc_read(chunk, head, &desc)) {
        return fail(failure, HTTPD_400, NULL, "not an app image");
    }
    if (strcmp(desc.project, push->project) != 0) {
        return fail(failure, HTTPD_400, NULL, "project differs from the image");
    }

    /* Proven and plausible: from here on the push shows, and erases. */
    failure->screen_asked = true;
    update_screen_take(0);
    esp_err_t result = install_begin(install, &desc, push->length, INSTALL_FROM_MAC);
    if (result == ESP_ERR_INVALID_SIZE) {
        return fail(failure, HTTPD_400, "too big", "too big for %s", install->partition->label);
    }
    if (result != ESP_OK) {
        return fail(failure, HTTPD_500, "flash error", "flash error %s", esp_err_to_name(result));
    }
    const char *label = install_is_launcher(install) ? UPDATE_LAUNCHER_LABEL : update_app_label();
    size_t received = head;
    result = install_write(install, chunk, head);
    while (result == ESP_OK && received < push->length) {
        show_progress(label, install);
        size_t left = push->length - received;
        size_t length = left < CHUNK_BYTES ? left : CHUNK_BYTES;
        if (!receive(request, chunk, length)) {
            return fail(failure, NULL, "transfer stopped", "transfer stopped");
        }
        result = install_write(install, chunk, length);
        received += length;
    }
    if (result != ESP_OK) {
        return fail(failure, HTTPD_500, "flash error", "flash error %s", esp_err_to_name(result));
    }
    show_progress(label, install);

    result = install_finish(install, push->sha256, push->commit_time, PUSHED_VERSION);
    if (result == ESP_ERR_INVALID_CRC) {
        return fail(failure, STATUS_UNPROCESSABLE, "hash differs", "image hash differs");
    }
    if (result == ESP_ERR_IMAGE_INVALID) {
        return fail(failure, STATUS_UNPROCESSABLE, "does not verify", "image does not verify");
    }
    if (result != ESP_OK) {
        return fail(failure, HTTPD_500, "flash error", "flash error %s", esp_err_to_name(result));
    }
    return true;
}

/* A for ota_0, B for ota_1, as the launcher's menu names them. */
static char slot_letter(const esp_partition_t *partition)
{
    return (char)('A' + (partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN));
}

/* For watching memory on the board, after the install's deepest calls. */
static void log_stack_left(void)
{
    ESP_LOGI(TAG, "Push done; %u bytes of the server's stack never used",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

/* A new app starts at once: the board answers, shows RESTARTING and restarts. */
static void restart_into(httpd_req_t *request, const install_t *install)
    __attribute__((noreturn));
static void restart_into(httpd_req_t *request, const install_t *install)
{
    char text[REPLY_SIZE];
    snprintf(text, sizeof(text), "installed %s in slot %c; restarting", install->desc.project,
             slot_letter(install->partition));
    reply(request, HTTPD_200, text);
    log_stack_left();
    update_screen_take(RESTART_SCREEN_WAIT_MS);
    update_screen_finish(UPDATE_TITLE_RESTARTING, NULL, UPDATE_RESTARTING_MS, true);
    esp_restart();
}

/* Everything after update_lock succeeded. Only this function unlocks, once,
   on every way out but the restart into a new app. */
static esp_err_t install_locked(httpd_req_t *request, const push_t *push,
                                push_challenge_t *offered, const char *key)
{
    install_t install;
    memset(&install, 0, sizeof(install)); /* install_abort is safe before install_begin */
    failure_t failure = {0};
    uint8_t *chunk = malloc(CHUNK_BYTES);
    bool installed =
        chunk != NULL
            ? receive_and_install(request, push, offered, key, chunk, &install, &failure)
            : fail(&failure, HTTPD_500, NULL, "flash error %s", esp_err_to_name(ESP_ERR_NO_MEM));
    free(chunk);
    if (installed && !install_is_launcher(&install)) {
        restart_into(request, &install);
    }

    esp_err_t sent;
    if (installed) {
        sent = reply(request, HTTPD_200, "installed the launcher; it runs at the next power-on");
        update_screen_finish(UPDATE_TITLE_LAUNCHER, UPDATE_DETAIL_LAUNCHER, UPDATE_LAUNCHER_MS,
                             false);
    } else {
        install_abort(&install);
        ESP_LOGW(TAG, "Push failed: %s", failure.text);
        sent = failure.status != NULL ? reply(request, failure.status, failure.text) : ESP_FAIL;
        if (failure.screen_asked) {
            update_screen_finish(UPDATE_TITLE_FAILED, failure.reason, UPDATE_FAILED_MS, false);
        }
    }
    log_stack_left();
    update_unlock();
    return sent;
}

/* GET /update/challenge: a new challenge, replacing any earlier one. */
static esp_err_t challenge_get(httpd_req_t *request)
{
    char key[KEY_SIZE];
    bool have_key = read_key(key);
    mbedtls_platform_zeroize(key, sizeof(key));
    if (!have_key) {
        return refuse(request, STATUS_FORBIDDEN, REPLY_NO_KEY);
    }
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    push_challenge_issue(&challenge, random, esp_timer_get_time());
    return reply(request, HTTPD_200, challenge.hex);
}

/* POST /update. The checks run in the protocol's order: key saved, headers,
   lock, then the rest in install_locked. */
static esp_err_t update_post(httpd_req_t *request)
{
    /* Any push uses up the live challenge, whatever its outcome; this copy
       is the one it is checked against. */
    push_challenge_t offered = challenge;
    challenge.live = false;

    char key[KEY_SIZE];
    push_t push;
    int busy_seconds;
    esp_err_t sent;
    if (!read_key(key)) {
        sent = refuse(request, STATUS_FORBIDDEN, REPLY_NO_KEY);
    } else if (!read_headers(request, &push)) {
        sent = refuse(request, HTTPD_400, "missing or malformed X-Cyd headers");
    } else if (!update_lock(&busy_seconds)) {
        /* Another update holds the lock, and maybe the screen: leave both alone. */
        char text[REPLY_SIZE];
        snprintf(text, sizeof(text), "busy %d", busy_seconds);
        sent = refuse(request, STATUS_CONFLICT, text);
    } else {
        sent = install_locked(request, &push, &offered, key);
    }
    mbedtls_platform_zeroize(key, sizeof(key));
    return sent;
}

static esp_err_t start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = SERVER_PORT;
    config.stack_size = SERVER_STACK;
    config.max_open_sockets = SERVER_SOCKETS;
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(&server, &config);
    if (result != ESP_OK) {
        server = NULL;
        return result;
    }
    const httpd_uri_t challenge_uri = {
        .uri = "/update/challenge", .method = HTTP_GET, .handler = challenge_get};
    const httpd_uri_t update_uri = {.uri = "/update", .method = HTTP_POST, .handler = update_post};
    result = httpd_register_uri_handler(server, &challenge_uri);
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(server, &update_uri);
    }
    if (result != ESP_OK) {
        httpd_stop(server);
        server = NULL;
    }
    return result;
}

/* cyd-xxxx.local, xxxx the end of the station MAC address, so the Mac finds
   the board without knowing its address. */
static esp_err_t start_mdns(void)
{
    uint8_t mac[6] = {0};
    esp_err_t result = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char host[16];
    char instance[40];
    snprintf(host, sizeof(host), "cyd-%02x%02x", mac[4], mac[5]);
    snprintf(instance, sizeof(instance), "CYD %s", update_app_label());
    if (result == ESP_OK) {
        result = mdns_init();
    }
    if (result == ESP_OK) {
        result = mdns_hostname_set(host);
    }
    if (result == ESP_OK) {
        result = mdns_instance_name_set(instance);
    }
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Pushes go to http://%s.local/update", host);
    }
    return result;
}

esp_err_t push_server_start(void)
{
    if (server == NULL) {
        esp_err_t result = start_server();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "The push server failed to start: %s", esp_err_to_name(result));
            return result;
        }
    }
    if (!mdns_running) {
        esp_err_t result = start_mdns();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "mDNS failed to start: %s; push to the board's address instead",
                     esp_err_to_name(result));
            return result;
        }
        mdns_running = true;
    }
    return ESP_OK;
}
