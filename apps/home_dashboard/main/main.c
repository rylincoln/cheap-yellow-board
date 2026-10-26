#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cyd_display.h"
#include "glance.h"
#include "ha_client.h"
#include "readings.h"
#include "wildlife.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "rom/tjpgd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define LCD_WIDTH CYD_LCD_WIDTH
#define LCD_HEIGHT CYD_LCD_HEIGHT
/* Home Assistant is asked this often while the status page or the glance
   screen is up. */
#define HA_INTERVAL_US (30LL * 1000 * 1000)
/* The glance screen re-composes its tiles this often, so a reading turns
   gray on time without a fetch. */
#define GLANCE_REDRAW_US (1000LL * 1000)
/* Settings the launcher (bootloader branch) stores in NVS for boot images,
   so no credentials are compiled into this published binary. */
#define CONFIG_NAMESPACE "cyd_config"
/* The wildlife camera's reply is read whole, up to this size. */
#define WILDLIFE_JSON_CAPACITY (8 * 1024)
#define IMAGE_JPEG_CAPACITY (320 * 1024)
#define JPEG_WORKBUF_SIZE 3100
#define IMAGE_TOP 0
#define IMAGE_HEIGHT LCD_HEIGHT
/* Notification bits for the dashboard task. */
#define TAP_BIT BIT0
#define WIFI_JOINED_BIT BIT1
/* A tap on the photo shows the bird's details this long, or until the next tap. */
#define INFO_TIMEOUT_US (30LL * 1000 * 1000)
/* The photo, and a message saying why there is none, give way to the glance
   screen after these. */
#define PHOTO_TIMEOUT_US (30LL * 1000 * 1000)
#define MESSAGE_TIMEOUT_US (5LL * 1000 * 1000)
#define COLOR_BLACK CYD_COLOR_BLACK

static const char *TAG = "home_dashboard";

static char wifi_ssid[33];
static char wifi_password[65];
/* The wildlife camera gallery's address, without a trailing slash; empty
   until phone setup stores one. */
static char wildlife_url[128];
static volatile bool wifi_connected;
static TaskHandle_t app_task_handle;

/* Home Assistant settings, the request built from them once, its reply,
   and the readings taken from the replies. */
static const char *const HA_SENSOR_KEYS[SENSOR_COUNT] = {
    [SENSOR_UPSTAIRS] = "ha_upstairs",
    [SENSOR_DOWNSTAIRS] = "ha_downstairs",
    [SENSOR_OUTSIDE] = "ha_outside",
    [SENSOR_BATTERY] = "ha_battery",
};
static char ha_url[128];
static char ha_token[256];
static char ha_entity_ids[SENSOR_COUNT][96];
static const char *const entity_ids[SENSOR_COUNT] = {
    [SENSOR_UPSTAIRS] = ha_entity_ids[SENSOR_UPSTAIRS],
    [SENSOR_DOWNSTAIRS] = ha_entity_ids[SENSOR_DOWNSTAIRS],
    [SENSOR_OUTSIDE] = ha_entity_ids[SENSOR_OUTSIDE],
    [SENSOR_BATTERY] = ha_entity_ids[SENSOR_BATTERY],
};
static char ha_body[768];
static char ha_reply[2048];
static reading_t readings[SENSOR_COUNT];
/* The Home Assistant setting that is missing, such as "no HA token", or
   NULL when the request is ready. */
static const char *ha_setup_problem;

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
    size_t limit;
} http_response_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    esp_http_client_handle_t client;
    bool read_error;
} jpeg_reader_t;

typedef struct {
    /* The decoded image is scaled to target_width x target_height and drawn
       with its top left corner at offset_x, offset_y. */
    int offset_x;
    int offset_y;
    int target_width;
    int target_height;
    unsigned int source_width;
    unsigned int source_height;
    uint8_t *rgb565;
    size_t buffer_capacity;
    /* Placement of the resized tile currently in rgb565. */
    int tile_left;
    int tile_top;
    int tile_width;
} jpeg_output_t;

typedef struct {
    jpeg_reader_t reader;
    jpeg_output_t output;
} jpeg_context_t;

/* ---- Status screen ----

   Until the first Home Assistant readings arrive, the screen shows how the
   dashboard is getting on: why it last started (a crash or watchdog reset
   boots straight back into this image, so this is how one shows), the
   Wi-Fi network and its state, the Home Assistant address and the last
   request. */

/* Static buffers, so sized for the widest board; drawing cuts to the panel. */
#define STATUS_SIZE (CYD_TEXT_MAX_COLUMNS + 1)
#define STATUS_MAGIC 0xB1DD5747u

/* What the viewer was doing, kept across crash and watchdog resets. */
typedef struct {
    uint32_t magic;
    uint32_t crashes; /* in a row */
    char stage[STATUS_SIZE];
} crash_record_t;

static RTC_NOINIT_ATTR crash_record_t crash_record;
static char start_status[STATUS_SIZE];
static char crash_status[STATUS_SIZE];
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static char wifi_status[STATUS_SIZE] = "starting";
static char ha_status[STATUS_SIZE] = "waiting for Wi-Fi";
/* The last wildlife camera request's result, for the WILDLIFE ERROR message. */
static char server_status[STATUS_SIZE];
static volatile uint32_t status_changes;
static uint32_t shown_status_changes;
/* False when something else has been drawn over the status screen. */
static bool status_on_screen;

static void set_stage(const char *stage)
{
    crash_record.magic = STATUS_MAGIC;
    snprintf(crash_record.stage, sizeof(crash_record.stage), "%s", stage);
}

/* Records why this start happened, and what a crashed run was doing. */
static void note_start(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    const char *crash = NULL;
    switch (reason) {
    case ESP_RST_PANIC:
        crash = "CRASHED";
        break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        crash = "WATCHDOG";
        break;
    case ESP_RST_BROWNOUT:
        crash = "BROWNOUT";
        break;
    default:
        break;
    }
    bool have_record = crash_record.magic == STATUS_MAGIC;
    if (crash == NULL) {
        snprintf(start_status, sizeof(start_status), "%s",
                 reason == ESP_RST_SW ? "started" : "powered on");
        crash_record.crashes = 0;
    } else {
        crash_record.crashes = have_record ? crash_record.crashes + 1 : 1;
        if (crash_record.crashes > 1) {
            snprintf(start_status, sizeof(start_status), "%.8s x%u", crash,
                     (unsigned)(crash_record.crashes % 100));
        } else {
            snprintf(start_status, sizeof(start_status), "%s", crash);
        }
        if (have_record) {
            crash_record.stage[sizeof(crash_record.stage) - 1] = '\0';
            snprintf(crash_status, sizeof(crash_status), "in %.14s", crash_record.stage);
        }
        ESP_LOGW(TAG, "Restarted after a %s (reason %d) while %s", crash, (int)reason,
                 have_record ? crash_record.stage : "unknown");
    }
    set_stage("starting");
}

static void set_status(char *field, const char *format, va_list arguments)
{
    char text[STATUS_SIZE];
    vsnprintf(text, sizeof(text), format, arguments);
    portENTER_CRITICAL(&status_lock);
    memcpy(field, text, sizeof(text));
    ++status_changes;
    portEXIT_CRITICAL(&status_lock);
}

static void set_wifi_status(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void set_wifi_status(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    set_status(wifi_status, format, arguments);
    va_end(arguments);
}

static void set_ha_status(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void set_ha_status(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    set_status(ha_status, format, arguments);
    va_end(arguments);
}

static void set_server_status(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void set_server_status(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    set_status(server_status, format, arguments);
    va_end(arguments);
}

static void draw_status(void)
{
    char wifi[STATUS_SIZE];
    char home_assistant[STATUS_SIZE];
    portENTER_CRITICAL(&status_lock);
    memcpy(wifi, wifi_status, sizeof(wifi));
    memcpy(home_assistant, ha_status, sizeof(home_assistant));
    shown_status_changes = status_changes;
    portEXIT_CRITICAL(&status_lock);

    char network[STATUS_SIZE + 16];
    snprintf(network, sizeof(network), "Wi-Fi %.11s", wifi_ssid);
    /* "HA homeassistant.local:8123": the address without its scheme or
       path, cut to the row. */
    const char *host = ha_url;
    if (strncmp(host, "http://", 7) == 0) {
        host += 7;
    } else if (strncmp(host, "https://", 8) == 0) {
        host += 8;
    }
    int host_length = (int)strcspn(host, "/");
    int host_room = CYD_TEXT_COLUMNS - 3; /* after "HA " */
    char address[STATUS_SIZE];
    snprintf(address, sizeof(address), "HA %.*s",
             host_length < host_room ? host_length : host_room, host);
    const char *hint = ha_setup_problem != NULL ? "use phone setup"
                       : wifi_connected         ? ""
                                                : "EN opens launcher";
    char memory[32];
    snprintf(memory, sizeof(memory), "RAM %uK free", (unsigned)(esp_get_free_heap_size() / 1024));
    bool crashed = crash_record.crashes > 0;
    const struct {
        uint16_t color;
        const char *text;
    } rows[CYD_TEXT_ROWS] = {
        {CYD_COLOR_YELLOW, "HOME DASHBOARD"},
        {crashed ? CYD_COLOR_RED : CYD_COLOR_GRAY, start_status},
        {CYD_COLOR_RED, crash_status},
        {CYD_COLOR_CYAN, network},
        {CYD_COLOR_WHITE, wifi},
        {CYD_COLOR_CYAN, address},
        {CYD_COLOR_WHITE, home_assistant},
        {CYD_COLOR_GRAY, hint},
        {CYD_COLOR_GRAY, memory},
        {CYD_COLOR_GRAY, "tap to retry"},
    };
    for (int row = 0; row < CYD_TEXT_ROWS; ++row) {
        ESP_ERROR_CHECK(cyd_text_row(row, rows[row].color, CYD_COLOR_BLACK, rows[row].text));
    }
    status_on_screen = true;
}

/* True while the bird info screen is up in place of the photo. */
static bool info_on_screen;

/* What the screen shows. The status page gives way to the glance screen at
   the first readings and never comes back; PHOTO, INFO and MESSAGE go back
   to the glance screen on a tap or a timeout. */
typedef enum {
    SCREEN_STATUS,
    SCREEN_GLANCE,
    SCREEN_PHOTO,
    SCREEN_INFO,
    SCREEN_MESSAGE,
} screen_t;

static screen_t screen = SCREEN_STATUS;

/* Redraws the status screen when it changed, while it is the screen. */
static void update_status_screen(void)
{
    if (screen == SCREEN_STATUS &&
        (!status_on_screen || status_changes != shown_status_changes)) {
        draw_status();
    }
}

/* Says what the viewer is doing, on screen and in the crash record. */
static void show_progress(const char *stage)
{
    set_stage(stage);
    set_server_status("%s", stage);
    update_status_screen();
}

/* The status of the last request that got an answer, else 0. */
static int last_http_status;

/* "visits HTTP 404", or the error without its ESP_ERR_ prefix. */
static void report_failure(const char *what, esp_err_t result)
{
    if (result == ESP_FAIL && last_http_status != 0 && last_http_status != 200) {
        set_server_status("%s HTTP %d", what, last_http_status);
    } else {
        const char *name = esp_err_to_name(result);
        set_server_status("%s %s", what, strncmp(name, "ESP_ERR_", 8) == 0 ? name + 8 : name);
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    http_response_t *response = event->user_data;
    size_t chunk_length = (size_t)event->data_len;
    if (response->length > response->limit ||
        chunk_length > response->limit - response->length) {
        ESP_LOGE(TAG, "HTTP response exceeded %u-byte limit",
                 (unsigned)response->limit);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t required = response->length + chunk_length;
    if (required > response->capacity) {
        size_t new_capacity = response->capacity * 2;
        if (new_capacity < required) {
            new_capacity = required;
        }
        if (new_capacity > response->limit) {
            new_capacity = response->limit;
        }
        uint8_t *new_data = heap_caps_realloc(response->data, new_capacity + 1,
                                              MALLOC_CAP_8BIT);
        if (new_data == NULL) {
            ESP_LOGE(TAG, "Could not grow HTTP response buffer to %u bytes",
                     (unsigned)new_capacity);
            return ESP_ERR_NO_MEM;
        }
        response->data = new_data;
        response->capacity = new_capacity;
    }
    memcpy(response->data + response->length, event->data, chunk_length);
    response->length += chunk_length;
    return ESP_OK;
}

static esp_err_t http_get(const char *url, size_t capacity, http_response_t *response)
{
    memset(response, 0, sizeof(*response));
    size_t initial_capacity = capacity < 2048 ? capacity : 2048;
    response->data = heap_caps_malloc(initial_capacity + 1, MALLOC_CAP_8BIT);
    if (response->data == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = initial_capacity;
    response->limit = capacity;

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = strncmp(url, "https://", 8) == 0
                                 ? esp_crt_bundle_attach
                                 : NULL,
        .event_handler = http_event_handler,
        .user_data = response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        heap_caps_free(response->data);
        response->data = NULL;
        return ESP_FAIL;
    }

    esp_err_t result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    last_http_status = result == ESP_OK ? status : 0;
    if (result == ESP_OK && status != 200) {
        ESP_LOGE(TAG, "GET %s returned HTTP %d", url, status);
        result = ESP_FAIL;
    }
    esp_http_client_cleanup(client);
    if (result != ESP_OK) {
        heap_caps_free(response->data);
        response->data = NULL;
        return result;
    }
    response->data[response->length] = '\0';
    return ESP_OK;
}

static void http_response_free(http_response_t *response)
{
    heap_caps_free(response->data);
    memset(response, 0, sizeof(*response));
}

/* Opens a GET of the JPEG at url and reads its headers, leaving *client
   ready to read a body of *length bytes. Requires HTTP 200 and a length
   from 1 to IMAGE_JPEG_CAPACITY. */
static esp_err_t open_image_stream(const char *url, esp_http_client_handle_t *client,
                                   size_t *length)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = strncmp(url, "https://", 8) == 0
                                 ? esp_crt_bundle_attach
                                 : NULL,
    };
    *client = esp_http_client_init(&config);
    if (*client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t result = esp_http_client_open(*client, 0);
    if (result != ESP_OK) {
        esp_http_client_cleanup(*client);
        *client = NULL;
        ESP_LOGE(TAG, "Could not open image stream: %s", esp_err_to_name(result));
        return result;
    }
    int64_t content_length = esp_http_client_fetch_headers(*client);
    int status = esp_http_client_get_status_code(*client);
    last_http_status = status;
    if (status != 200 || content_length <= 0 ||
        content_length > IMAGE_JPEG_CAPACITY) {
        ESP_LOGE(TAG, "Image stream returned HTTP %d and %u bytes",
                 status, (unsigned)(content_length < 0 ? 0 : content_length));
        esp_http_client_close(*client);
        esp_http_client_cleanup(*client);
        *client = NULL;
        return ESP_ERR_INVALID_RESPONSE;
    }
    *length = (size_t)content_length;
    ESP_LOGI(TAG, "Opened %u-byte image stream; free heap: %u bytes",
             (unsigned)*length, (unsigned)esp_get_free_heap_size());
    return ESP_OK;
}

static UINT jpeg_input(JDEC *decoder, BYTE *buffer, UINT length)
{
    jpeg_context_t *context = decoder->device;
    jpeg_reader_t *reader = &context->reader;
    if (reader->offset >= reader->length) {
        return 0;
    }

    size_t wanted = length;
    size_t remaining = reader->length - reader->offset;
    if (wanted > remaining) {
        wanted = remaining;
    }
    uint8_t discard[64];
    size_t received = 0;
    while (received < wanted) {
        size_t chunk = wanted - received;
        if (buffer == NULL && chunk > sizeof(discard)) {
            chunk = sizeof(discard);
        }
        char *destination = buffer != NULL
                                ? (char *)buffer + received
                                : (char *)discard;
        int bytes = esp_http_client_read(reader->client, destination, (int)chunk);
        if (bytes <= 0) {
            reader->read_error = true;
            ESP_LOGE(TAG, "Image stream ended during JPEG decode");
            break;
        }
        received += (size_t)bytes;
    }
    reader->offset += received;
    return (UINT)received;
}

static uint16_t jpeg_tile_pixel(int x, int y, void *context)
{
    const jpeg_output_t *output = context;
    size_t index = ((size_t)(y - output->tile_top) * output->tile_width +
                    (x - output->tile_left)) * 2;
    return (uint16_t)((output->rgb565[index] << 8) | output->rgb565[index + 1]);
}

static UINT jpeg_output(JDEC *decoder, void *pixels, JRECT *rectangle)
{
    jpeg_context_t *context = decoder->device;
    jpeg_output_t *output = &context->output;
    int width = rectangle->right - rectangle->left + 1;
    int height = rectangle->bottom - rectangle->top + 1;
    if (width <= 0 || height <= 0 || width > 16 || height > 16) {
        ESP_LOGE(TAG, "Unexpected JPEG output tile %dx%d", width, height);
        return 0;
    }

    const uint8_t *rgb = pixels;
    int left = ((size_t)rectangle->left * output->target_width +
                output->source_width - 1) / output->source_width;
    int right = ((size_t)(rectangle->right + 1) * output->target_width +
                 output->source_width - 1) / output->source_width;
    int top = ((size_t)rectangle->top * output->target_height +
               output->source_height - 1) / output->source_height;
    int bottom = ((size_t)(rectangle->bottom + 1) * output->target_height +
                  output->source_height - 1) / output->source_height;
    int output_width = right - left;
    int output_height = bottom - top;
    if (output_width <= 0 || output_height <= 0) {
        return 1; /* a sliver at the image's edge that shrinks to nothing */
    }
    if ((size_t)output_width * output_height * 2 > output->buffer_capacity) {
        ESP_LOGE(TAG, "Resized JPEG tile %dx%d exceeds its output buffer",
                 output_width, output_height);
        return 0;
    }
    for (int row = 0; row < output_height; ++row) {
        int source_y = (top + row) * output->source_height / output->target_height -
                       rectangle->top;
        for (int column = 0; column < output_width; ++column) {
            int source_x = (left + column) * output->source_width / output->target_width -
                           rectangle->left;
            int source_index = (source_y * width + source_x) * 3;
            uint8_t red = rgb[source_index];
            uint8_t green = rgb[source_index + 1];
            uint8_t blue = rgb[source_index + 2];
            uint16_t color = (uint16_t)(((red & 0xF8) << 8) |
                                        ((green & 0xFC) << 3) | (blue >> 3));
            int target_index = (row * output_width + column) * 2;
            output->rgb565[target_index] = (uint8_t)(color >> 8);
            output->rgb565[target_index + 1] = (uint8_t)color;
        }
    }
    /* cyd_draw_pattern sends pixels in the panel's column-major order. */
    output->tile_left = output->offset_x + left;
    output->tile_top = output->offset_y + top;
    output->tile_width = output_width;
    if (cyd_draw_pattern(output->tile_left, output->tile_top, output_width, output_height,
                         jpeg_tile_pixel, output) != ESP_OK) {
        return 0;
    }
    return 1;
}

static esp_err_t draw_jpeg(esp_http_client_handle_t image_client, size_t length)
{
    jpeg_context_t context = {
        .reader = {.length = length, .client = image_client},
    };
    JDEC decoder = {0};
    void *work_buffer = heap_caps_malloc(JPEG_WORKBUF_SIZE, MALLOC_CAP_8BIT);
    if (work_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    JRESULT result = jd_prepare(&decoder, jpeg_input, work_buffer,
                                JPEG_WORKBUF_SIZE, &context);
    if (result != JDR_OK || decoder.width == 0 || decoder.height == 0) {
        heap_caps_free(work_buffer);
        ESP_LOGE(TAG, "JPEG prepare failed: %d", result);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Fit the image inside the screen, keeping its aspect ratio: a 330x220
       photo fills a 480x320 screen, and a 330x499 one is 212x320, centred
       on black. */
    uint32_t image_width = decoder.width;
    uint32_t image_height = decoder.height;
    int fit_width = LCD_WIDTH;
    int fit_height = IMAGE_HEIGHT;
    if (image_width * (uint32_t)IMAGE_HEIGHT > image_height * (uint32_t)LCD_WIDTH) {
        fit_height = (int)((image_height * LCD_WIDTH + image_width / 2) / image_width);
    } else {
        fit_width = (int)((image_width * IMAGE_HEIGHT + image_height / 2) / image_height);
    }
    fit_width = fit_width > 0 ? fit_width : 1;
    fit_height = fit_height > 0 ? fit_height : 1;

    /* Decode at the smallest 1/2^n size still at least as large as the fit
       both ways, so the tile scaler only shrinks unless the photo is smaller
       than its fit: 1280x853 decodes at 1/2 to fit 480x320 and at 1/4 to
       fit 320x213. */
    BYTE scale = 0;
    while (scale < 3 &&
           (int)((decoder.width + (2U << scale) - 1) >> (scale + 1)) >= fit_width &&
           (int)((decoder.height + (2U << scale) - 1) >> (scale + 1)) >= fit_height) {
        ++scale;
    }
    int scaled_width = (int)((decoder.width + (1U << scale) - 1) >> scale);
    int scaled_height = (int)((decoder.height + (1U << scale) - 1) >> scale);
    size_t tile_width = ((size_t)16 * fit_width + scaled_width - 1) / scaled_width + 1;
    size_t tile_height = ((size_t)16 * fit_height + scaled_height - 1) / scaled_height + 1;
    if (tile_width > 64 || tile_height > 64) {
        heap_caps_free(work_buffer);
        ESP_LOGE(TAG, "JPEG is too small to scale up to %dx%d", fit_width, fit_height);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t output_capacity = tile_width * tile_height * 2;
    uint8_t *rgb565 = heap_caps_malloc(output_capacity, MALLOC_CAP_DMA);
    if (rgb565 == NULL) {
        heap_caps_free(work_buffer);
        return ESP_ERR_NO_MEM;
    }
    context.output = (jpeg_output_t){
        .offset_x = (LCD_WIDTH - fit_width) / 2,
        .offset_y = IMAGE_TOP + (IMAGE_HEIGHT - fit_height) / 2,
        .target_width = fit_width,
        .target_height = fit_height,
        .source_width = (unsigned int)scaled_width,
        .source_height = (unsigned int)scaled_height,
        .rgb565 = rgb565,
        .buffer_capacity = output_capacity,
    };
    status_on_screen = false;
    if (cyd_fill_rect(0, IMAGE_TOP, LCD_WIDTH, IMAGE_HEIGHT, COLOR_BLACK) != ESP_OK) {
        heap_caps_free(rgb565);
        heap_caps_free(work_buffer);
        return ESP_FAIL;
    }
    result = jd_decomp(&decoder, jpeg_output, scale);
    heap_caps_free(rgb565);
    heap_caps_free(work_buffer);
    if (result != JDR_OK || context.reader.read_error) {
        ESP_LOGE(TAG, "JPEG decode failed: %d", result);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Rendered %ux%u bird image at 1:%u scale as %dx%d",
             (unsigned)decoder.width, (unsigned)decoder.height, 1U << scale, fit_width,
             fit_height);
    return ESP_OK;
}

/* The wildlife camera's latest birds, newest first, kept for the details
   page. */
static wildlife_bird_t birds[WILDLIFE_MAX_BIRDS];
static int bird_count;

typedef enum { BIRD_SHOWN, BIRD_NO_PHOTO, BIRD_NONE, BIRD_FAILED } bird_result_t;

/* Asks the wildlife camera for its latest birds. Returns false, with
   server_status saying why, when the request fails or the reply can't be
   read. */
static bool fetch_birds(void)
{
    char url[sizeof(wildlife_url) + sizeof("/frame/wildlife")];
    snprintf(url, sizeof(url), "%s/frame/wildlife", wildlife_url);
    http_response_t response;
    esp_err_t result = http_get(url, WILDLIFE_JSON_CAPACITY, &response);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not fetch the wildlife camera's birds: %s",
                 esp_err_to_name(result));
        report_failure("birds", result);
        return false;
    }
    int count = wildlife_parse_birds((const char *)response.data, response.length, birds,
                                     WILDLIFE_MAX_BIRDS);
    http_response_free(&response);
    bird_count = count > 0 ? count : 0;
    if (count < 0) {
        ESP_LOGE(TAG, "The wildlife camera's reply has no readable birds array");
        set_server_status("bad reply");
        return false;
    }
    ESP_LOGI(TAG, "The wildlife camera has %d birds", count);
    return true;
}

/* Streams the bird's photo from the wildlife camera, fitted to the screen.
   Returns false, with the reason logged, when it has no photo or the photo
   can't be fetched or drawn. */
static bool show_bird_photo(const wildlife_bird_t *bird)
{
    char url[sizeof(wildlife_url) + sizeof(bird->photo_url)];
    if (!wildlife_join_url(wildlife_url, bird->photo_url, url, sizeof(url))) {
        ESP_LOGW(TAG, "No photo of %s", bird->label);
        return false;
    }
    esp_http_client_handle_t image_client = NULL;
    size_t jpeg_length = 0;
    show_progress("getting photo");
    esp_err_t result = open_image_stream(url, &image_client, &jpeg_length);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Could not get the photo of %s: %s", bird->label,
                 esp_err_to_name(result));
        return false;
    }
    set_stage("drawing photo");
    result = draw_jpeg(image_client, jpeg_length);
    esp_http_client_close(image_client);
    esp_http_client_cleanup(image_client);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Could not draw the photo of %s: %s", bird->label,
                 esp_err_to_name(result));
        return false;
    }
    return true;
}

/* Fetches the wildlife camera's latest birds and streams the newest one's
   photo full screen. On BIRD_FAILED, server_status says why. */
static bird_result_t show_latest_bird(void)
{
    show_progress("getting birds");
    if (!fetch_birds()) {
        set_stage("idle");
        return BIRD_FAILED;
    }
    if (bird_count == 0) {
        set_stage("idle");
        return BIRD_NONE;
    }
    bool shown = show_bird_photo(&birds[0]);
    set_stage("idle");
    return shown ? BIRD_SHOWN : BIRD_NO_PHOTO;
}

/* ---- Bird info screen ---- */

/* Birds listed under "also heard", on rows 5 to 8. */
#define ALSO_HEARD_ROWS 4

/* Splits a name at the last space that keeps the first line within one row. */
static void wrap_name(const char *name, char *first, char *second, size_t capacity)
{
    size_t length = strlen(name);
    size_t split = length;
    if (length > CYD_TEXT_COLUMNS) {
        split = CYD_TEXT_COLUMNS;
        while (split > 0 && name[split] != ' ') {
            --split;
        }
        if (split == 0) {
            split = CYD_TEXT_COLUMNS;
        }
    }
    snprintf(first, capacity, "%.*s", (int)split, name);
    snprintf(second, capacity, "%s", name + split + (name[split] == ' ' ? 1 : 0));
}

/* Shows the latest birds at once, without a request: "Steller's Jay /
   4:42 PM / confidence 75% / also heard / Common Raven 3:10 PM ...". */
static void show_bird_info(void)
{
    const wildlife_bird_t *latest = &birds[0];
    char name[2][sizeof(latest->label)];
    char confidence[sizeof(latest->confidence) + 16];
    char heard[ALSO_HEARD_ROWS][CYD_TEXT_MAX_COLUMNS + 1];
    info_on_screen = true;
    status_on_screen = false;
    set_stage("showing info");
    wrap_name(latest->label, name[0], name[1], sizeof(name[0]));
    if (latest->confidence[0] != '\0') {
        snprintf(confidence, sizeof(confidence), "confidence %s", latest->confidence);
    } else {
        confidence[0] = '\0';
    }
    for (int index = 0; index < ALSO_HEARD_ROWS; ++index) {
        heard[index][0] = '\0';
        if (1 + index < bird_count) {
            wildlife_heard_row(&birds[1 + index], CYD_TEXT_COLUMNS, heard[index],
                               sizeof(heard[index]));
        }
    }
    const struct {
        uint16_t color;
        const char *text;
    } rows[CYD_TEXT_ROWS] = {
        {CYD_COLOR_YELLOW, name[0]},
        {CYD_COLOR_YELLOW, name[1]},
        {CYD_COLOR_WHITE, latest->time},
        {CYD_COLOR_WHITE, confidence},
        {CYD_COLOR_GRAY, bird_count > 1 ? "also heard" : ""},
        {CYD_COLOR_WHITE, heard[0]},
        {CYD_COLOR_WHITE, heard[1]},
        {CYD_COLOR_WHITE, heard[2]},
        {CYD_COLOR_WHITE, heard[3]},
        {CYD_COLOR_GRAY, "tap for readings"},
    };
    for (int row = 0; row < CYD_TEXT_ROWS; ++row) {
        ESP_ERROR_CHECK(cyd_text_row(row, rows[row].color, CYD_COLOR_BLACK, rows[row].text));
    }
    set_stage("idle");
}

/* ---- Home Assistant and the glance screen ---- */

/* When PHOTO, INFO or MESSAGE gives way to the glance screen. */
static int64_t screen_until_us;
/* When the glance screen last composed its tiles. */
static int64_t glance_composed_us;

/* Asks Home Assistant for the readings; returns true when at least one
   sensor is available. A failure only logs and sets the HA status, and
   leaves the readings as they were. */
static bool fetch_readings(void)
{
    int http_status = 0;
    set_stage("asking HA");
    set_ha_status("asking HA");
    update_status_screen();
    esp_err_t result = ha_fetch(ha_url, ha_token, ha_body, ha_reply, sizeof(ha_reply),
                                &http_status);
    set_stage("idle");
    if (result != ESP_OK) {
        if (http_status == 401) {
            set_ha_status("HA 401 bad token");
        } else if (http_status != 0 && http_status != 200) {
            set_ha_status("HA HTTP %d", http_status);
        } else {
            const char *name = esp_err_to_name(result);
            set_ha_status("HA %s", strncmp(name, "ESP_ERR_", 8) == 0 ? name + 8 : name);
        }
        update_status_screen();
        return false;
    }
    int found = readings_apply(ha_reply, entity_ids, esp_timer_get_time(), readings);
    ESP_LOGI(TAG, "Home Assistant has %d of %d sensors", found, SENSOR_COUNT);
    if (found == 0) {
        set_ha_status("no sensors found");
        update_status_screen();
        return false;
    }
    set_ha_status("HA OK");
    return true;
}

/* Composes the tiles from the readings and draws them: all of them on a
   cleared screen with full, else only those that changed. */
static void draw_glance(bool full)
{
    static glance_tile_t tiles[SENSOR_COUNT];
    int64_t now = esp_timer_get_time();
    glance_compose(LCD_WIDTH, LCD_HEIGHT, readings, now, tiles);
    glance_draw(tiles, full);
    glance_composed_us = now;
}

/* Shows the glance screen, drawn in full. */
static void enter_glance(void)
{
    screen = SCREEN_GLANCE;
    status_on_screen = false;
    info_on_screen = false;
    draw_glance(true);
}

/* Moves to a screen that gives way to the glance screen after timeout_us. */
static void enter_timed_screen(screen_t next, int64_t timeout_us)
{
    screen = next;
    screen_until_us = esp_timer_get_time() + timeout_us;
}

/* Clears the screen to two lines of text on rows 4 and 5. */
static void draw_lines(uint16_t color, const char *first, const char *second)
{
    ESP_ERROR_CHECK(cyd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK));
    ESP_ERROR_CHECK(cyd_text_row(4, color, COLOR_BLACK, first));
    ESP_ERROR_CHECK(cyd_text_row(5, color, COLOR_BLACK, second));
}

/* Says for MESSAGE_TIMEOUT_US why no bird is shown. */
static void show_message(const char *first, const char *second)
{
    draw_lines(CYD_COLOR_YELLOW, first, second);
    enter_timed_screen(SCREEN_MESSAGE, MESSAGE_TIMEOUT_US);
}

/* Answers a tap on the glance screen at once, then shows the latest bird's
   photo, or its details when there is no photo, or says why there is no
   bird. */
static void show_bird_for_tap(void)
{
    draw_lines(CYD_COLOR_WHITE, "finding the", "latest bird");
    if (!wifi_connected) {
        show_message("NO WI-FI", "");
        return;
    }
    if (wildlife_url[0] == '\0') {
        show_message("NO WILDLIFE CAM", "use phone setup");
        return;
    }
    char reason[STATUS_SIZE];
    switch (show_latest_bird()) {
    case BIRD_SHOWN:
        enter_timed_screen(SCREEN_PHOTO, PHOTO_TIMEOUT_US);
        break;
    case BIRD_NO_PHOTO:
        show_bird_info();
        enter_timed_screen(SCREEN_INFO, INFO_TIMEOUT_US);
        break;
    case BIRD_NONE:
        show_message("NO BIRDS YET", "");
        break;
    case BIRD_FAILED:
        portENTER_CRITICAL(&status_lock);
        memcpy(reason, server_status, sizeof(reason));
        portEXIT_CRITICAL(&status_lock);
        show_message("WILDLIFE ERROR", reason);
        break;
    }
}

/* Passes taps from the shared touchscreen driver to the dashboard task. */
static void touch_task(void *argument)
{
    QueueHandle_t touch_queue = argument;
    while (true) {
        cyd_touch_point_t point;
        if (xQueueReceive(touch_queue, &point, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Touch at %d,%d", point.x, point.y);
            if (app_task_handle != NULL) {
                xTaskNotify(app_task_handle, TAP_BIT, eSetBits);
            }
        }
    }
}

/* Why a join failed, in a few words. */
static const char *wifi_failure_text(uint8_t reason, char *text, size_t size)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return "no AP found";
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "bad password?";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "signal lost";
    default:
        snprintf(text, size, "error %u", (unsigned)reason);
        return text;
    }
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    static unsigned attempts;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        set_wifi_status("joining");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = event_data;
        char text[16];
        if (wifi_connected && ha_setup_problem == NULL) {
            set_ha_status("waiting for Wi-Fi");
        }
        wifi_connected = false;
        ++attempts;
        set_wifi_status("#%u %s", attempts,
                        wifi_failure_text(disconnected->reason, text, sizeof(text)));
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u); retrying",
                 (unsigned)disconnected->reason);
        /* Retrying never gives up, so a failed attempt must not abort. */
        esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi reconnect failed: %s", esp_err_to_name(result));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;
        wifi_connected = true;
        attempts = 0;
        set_wifi_status(IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Wi-Fi connected; IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        if (app_task_handle != NULL) {
            xTaskNotify(app_task_handle, WIFI_JOINED_BIT, eSetBits);
        }
    }
}

/* Drops the trailing slashes from a base URL. */
static void strip_trailing_slashes(char *url)
{
    size_t length = strlen(url);
    while (length > 0 && url[length - 1] == '/') {
        url[--length] = '\0';
    }
}

/* Names the first Home Assistant setting that is missing, or builds the
   request body and returns NULL. */
static const char *prepare_ha_request(void)
{
    if (ha_url[0] == '\0') {
        return "no HA address";
    }
    if (ha_token[0] == '\0') {
        return "no HA token";
    }
    for (int sensor = 0; sensor < SENSOR_COUNT; ++sensor) {
        if (ha_entity_ids[sensor][0] == '\0') {
            return "no sensor IDs";
        }
    }
    if (readings_template_body(entity_ids, ha_body, sizeof(ha_body)) < 0) {
        return "sensor IDs too long";
    }
    return NULL;
}

/* Reads the Wi-Fi, wildlife camera and Home Assistant settings from NVS;
   returns false when there are no Wi-Fi settings yet. */
static bool load_config(void)
{
    nvs_handle_t nvs;
    if (nvs_flash_init() != ESP_OK || nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(wifi_ssid);
    bool have_wifi = nvs_get_str(nvs, "wifi_ssid", wifi_ssid, &size) == ESP_OK &&
                     wifi_ssid[0] != '\0';
    size = sizeof(wifi_password);
    if (nvs_get_str(nvs, "wifi_pass", wifi_password, &size) != ESP_OK) {
        wifi_password[0] = '\0';
    }
    size = sizeof(wildlife_url);
    if (nvs_get_str(nvs, "wildlife_url", wildlife_url, &size) != ESP_OK) {
        wildlife_url[0] = '\0';
    }
    size = sizeof(ha_url);
    if (nvs_get_str(nvs, "ha_url", ha_url, &size) != ESP_OK) {
        ha_url[0] = '\0';
    }
    size = sizeof(ha_token);
    if (nvs_get_str(nvs, "ha_token", ha_token, &size) != ESP_OK) {
        ha_token[0] = '\0';
    }
    for (int sensor = 0; sensor < SENSOR_COUNT; ++sensor) {
        size = sizeof(ha_entity_ids[sensor]);
        if (nvs_get_str(nvs, HA_SENSOR_KEYS[sensor], ha_entity_ids[sensor], &size) != ESP_OK) {
            ha_entity_ids[sensor][0] = '\0';
        }
    }
    nvs_close(nvs);
    strip_trailing_slashes(wildlife_url);
    strip_trailing_slashes(ha_url);
    ESP_LOGI(TAG, "Wildlife camera: %s", wildlife_url[0] != '\0' ? wildlife_url : "not set");
    ha_setup_problem = prepare_ha_request();
    if (ha_setup_problem != NULL) {
        set_ha_status("%s", ha_setup_problem);
        ESP_LOGW(TAG, "Home Assistant is not set up (%s); use phone setup", ha_setup_problem);
    }
    return have_wifi;
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() != NULL ? ESP_OK : ESP_FAIL);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    /* An open network has no password and would fail a WPA2 threshold. */
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = wifi_password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
        },
    };
    memcpy(wifi_config.sta.ssid, wifi_ssid, strnlen(wifi_ssid, sizeof(wifi_config.sta.ssid)));
    memcpy(wifi_config.sta.password, wifi_password,
           strnlen(wifi_password, sizeof(wifi_config.sta.password) - 1));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());
}

/* The earlier of wake_us and deadline_us, counting only a deadline still
   ahead of now. */
static int64_t sooner(int64_t wake_us, int64_t deadline_us, int64_t now)
{
    return deadline_us > now && deadline_us < wake_us ? deadline_us : wake_us;
}

/* Runs the screens. Home Assistant is asked on joining Wi-Fi and every
   HA_INTERVAL_US while the status page or the glance screen is up. A tap on
   the glance screen shows the latest bird, a tap on the photo its details,
   and a tap on those, or a timeout, goes back to the glance screen. */
static void dashboard_task(void *argument)
{
    (void)argument;
    app_task_handle = xTaskGetCurrentTaskHandle();
    int64_t next_fetch_us = 0;
    while (true) {
        update_status_screen();
        bool timed = screen == SCREEN_PHOTO || screen == SCREEN_INFO || screen == SCREEN_MESSAGE;
        if (timed && esp_timer_get_time() >= screen_until_us) {
            enter_glance();
        }
        if ((screen == SCREEN_STATUS || screen == SCREEN_GLANCE) && ha_setup_problem == NULL &&
            wifi_connected && esp_timer_get_time() >= next_fetch_us) {
            bool arrived = fetch_readings();
            next_fetch_us = esp_timer_get_time() + HA_INTERVAL_US;
            if (arrived && screen == SCREEN_STATUS) {
                enter_glance();
                /* A tap made on the status page doesn't ask for a bird. */
                ulTaskNotifyValueClear(NULL, TAP_BIT);
            }
        }
        if (screen == SCREEN_GLANCE &&
            esp_timer_get_time() - glance_composed_us >= GLANCE_REDRAW_US) {
            draw_glance(false);
        }

        /* Wake at the screen's next deadline, and at least once a second. */
        int64_t now = esp_timer_get_time();
        int64_t wake_us = now + 1000 * 1000;
        if (screen == SCREEN_STATUS || screen == SCREEN_GLANCE) {
            wake_us = sooner(wake_us, next_fetch_us, now);
        }
        if (screen == SCREEN_GLANCE) {
            wake_us = sooner(wake_us, glance_composed_us + GLANCE_REDRAW_US, now);
        } else if (screen != SCREEN_STATUS) {
            wake_us = sooner(wake_us, screen_until_us, now);
        }
        TickType_t wait_ticks = pdMS_TO_TICKS((uint32_t)((wake_us - now) / 1000));
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }
        uint32_t events = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &events, wait_ticks) != pdTRUE) {
            continue;
        }
        if ((events & WIFI_JOINED_BIT) && screen == SCREEN_STATUS) {
            next_fetch_us = 0;
        }
        if (events & TAP_BIT) {
            switch (screen) {
            case SCREEN_STATUS:
                next_fetch_us = 0;
                break;
            case SCREEN_GLANCE:
                show_bird_for_tap();
                break;
            case SCREEN_PHOTO:
                show_bird_info();
                enter_timed_screen(SCREEN_INFO, INFO_TIMEOUT_US);
                break;
            case SCREEN_INFO:
            case SCREEN_MESSAGE:
                enter_glance();
                break;
            }
            /* Taps made while the screen was busy are dropped. */
            ulTaskNotifyValueClear(NULL, TAP_BIT);
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(cyd_display_init());
    note_start();
    if (!load_config()) {
        ESP_LOGE(TAG, "No Wi-Fi settings in NVS; the launcher's Wi-Fi setup stores them");
        ESP_ERROR_CHECK(cyd_text_row(3, CYD_COLOR_YELLOW, CYD_COLOR_BLACK, "NO WI-FI SETTINGS"));
        ESP_ERROR_CHECK(cyd_text_row(5, CYD_COLOR_WHITE, CYD_COLOR_BLACK, "Press EN to open"));
        ESP_ERROR_CHECK(cyd_text_row(6, CYD_COLOR_WHITE, CYD_COLOR_BLACK, "Wi-Fi setup"));
        return;
    }

    QueueHandle_t touch_queue;
    ESP_ERROR_CHECK(cyd_touch_start(&touch_queue));
    BaseType_t touch_task_result =
        xTaskCreate(touch_task, "touch", 3072, touch_queue, 5, NULL);
    ESP_ERROR_CHECK(touch_task_result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    BaseType_t dashboard_task_result =
        xTaskCreate(dashboard_task, "dashboard", 8192, NULL, 5, &app_task_handle);
    ESP_ERROR_CHECK(dashboard_task_result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init();
}
