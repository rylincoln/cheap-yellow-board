#include "wildlife_view.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_status.h"
#include "board_wifi.h"
#include "cyd_display.h"
#include "wildlife.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "rom/tjpgd.h"

#define LCD_WIDTH CYD_LCD_WIDTH
#define LCD_HEIGHT CYD_LCD_HEIGHT
/* The wildlife camera's reply is read up to this size; see http_get. */
#define WILDLIFE_JSON_CAPACITY (16 * 1024)
#define IMAGE_JPEG_CAPACITY (320 * 1024)
#define JPEG_WORKBUF_SIZE 3100
#define IMAGE_TOP 0
#define IMAGE_HEIGHT LCD_HEIGHT
#define COLOR_BLACK CYD_COLOR_BLACK

static const char *TAG = "wildlife";

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
    size_t limit;
    /* Set at the first chunk that doesn't fit: ESP_ERR_INVALID_SIZE when the
       reply passes limit, ESP_ERR_NO_MEM when the buffer can't grow. Every
       later chunk is dropped, so data stays an unbroken prefix of the reply. */
    esp_err_t stopped;
} http_response_t;

typedef struct {
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

/* Why the last bird lookup failed, for the WILDLIFE ERROR message. */
static char server_status[BOARD_STATUS_SIZE];

static void set_server_status(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void set_server_status(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(server_status, sizeof(server_status), format, arguments);
    va_end(arguments);
}

/* The status of the last request that got an answer, else 0. */
static int last_http_status;

/* "birds HTTP 404", or the error without its ESP_ERR_ prefix. */
static void report_failure(const char *what, esp_err_t result)
{
    if (result == ESP_FAIL && last_http_status != 0 && last_http_status != 200) {
        set_server_status("%s HTTP %d", what, last_http_status);
    } else {
        const char *name = esp_err_to_name(result);
        set_server_status("%s %s", what, strncmp(name, "ESP_ERR_", 8) == 0 ? name + 8 : name);
    }
}

/* Collects the body into response. IDF ignores this handler's return value
   and keeps sending chunks, so stopping is sticky: the chunk that passes the
   limit is cut at it and logged once, and every later chunk is dropped. The
   body is then a clean prefix of the reply, cut at the capacity. */
static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    http_response_t *response = event->user_data;
    if (response->stopped != ESP_OK) {
        return ESP_OK;
    }
    size_t chunk_length = (size_t)event->data_len;
    if (chunk_length > response->limit - response->length) {
        ESP_LOGW(TAG, "HTTP reply is over %u bytes; keeping only those",
                 (unsigned)response->limit);
        response->stopped = ESP_ERR_INVALID_SIZE;
        chunk_length = response->limit - response->length;
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
            response->stopped = ESP_ERR_NO_MEM;
            return ESP_OK;
        }
        response->data = new_data;
        response->capacity = new_capacity;
    }
    memcpy(response->data + response->length, event->data, chunk_length);
    response->length += chunk_length;
    return ESP_OK;
}

/* GETs url, reading at most capacity bytes of the body into
   response->data, with a NUL after them. A longer reply still returns
   ESP_OK, with the first capacity bytes and response->stopped set to
   ESP_ERR_INVALID_SIZE: the wildlife camera's reply puts "birds" first
   (Flask sorts its keys), and the parser stops at the end of that array or
   at WILDLIFE_MAX_BIRDS birds, so a cut reply still gives the newest birds.
   Fails on a request error, a status other than 200, or no memory. */
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
    if (result == ESP_OK && response->stopped == ESP_ERR_NO_MEM) {
        result = ESP_ERR_NO_MEM;
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

/* Asks the wildlife camera at base_url for its latest birds. Returns false,
   with server_status saying why, when the request fails or the reply can't
   be read. */
static bool fetch_birds(const char *base_url)
{
    char url[WILDLIFE_URL_SIZE + sizeof("/frame/wildlife")];
    snprintf(url, sizeof(url), "%s/frame/wildlife", base_url);
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
static bool show_bird_photo(const char *base_url, const wildlife_bird_t *bird)
{
    char url[WILDLIFE_URL_SIZE + sizeof(bird->photo_url)];
    if (!wildlife_join_url(base_url, bird->photo_url, url, sizeof(url))) {
        ESP_LOGW(TAG, "No photo of %s", bird->label);
        return false;
    }
    esp_http_client_handle_t image_client = NULL;
    size_t jpeg_length = 0;
    board_status_set_stage("getting photo");
    esp_err_t result = open_image_stream(url, &image_client, &jpeg_length);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Could not get the photo of %s: %s", bird->label,
                 esp_err_to_name(result));
        return false;
    }
    board_status_set_stage("drawing photo");
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
static bird_result_t show_latest_bird(const char *base_url)
{
    board_status_set_stage("getting birds");
    if (!fetch_birds(base_url)) {
        board_status_set_stage("idle");
        return BIRD_FAILED;
    }
    if (bird_count == 0) {
        board_status_set_stage("idle");
        return BIRD_NONE;
    }
    bool shown = show_bird_photo(base_url, &birds[0]);
    board_status_set_stage("idle");
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
void wildlife_show_info(void)
{
    const wildlife_bird_t *latest = &birds[0];
    char name[2][sizeof(latest->label)];
    char confidence[sizeof(latest->confidence) + 16];
    char heard[ALSO_HEARD_ROWS][CYD_TEXT_MAX_COLUMNS + 1];
    board_status_set_stage("showing info");
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
    board_status_set_stage("idle");
}

/* Clears the screen to two lines of text on rows 4 and 5. */
static void draw_lines(uint16_t color, const char *first, const char *second)
{
    ESP_ERROR_CHECK(cyd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK));
    ESP_ERROR_CHECK(cyd_text_row(4, color, COLOR_BLACK, first));
    ESP_ERROR_CHECK(cyd_text_row(5, color, COLOR_BLACK, second));
}

/* Says why no bird is shown. */
static wildlife_screen_t show_message(const char *first, const char *second)
{
    draw_lines(CYD_COLOR_YELLOW, first, second);
    return WILDLIFE_MESSAGE;
}

wildlife_screen_t wildlife_show_for_tap(const char *base_url)
{
    draw_lines(CYD_COLOR_WHITE, "finding the", "latest bird");
    if (!board_wifi_connected()) {
        return show_message("NO WI-FI", "");
    }
    if (base_url[0] == '\0') {
        return show_message("NO WILDLIFE CAM", "use phone setup");
    }
    switch (show_latest_bird(base_url)) {
    case BIRD_SHOWN:
        return WILDLIFE_PHOTO;
    case BIRD_NO_PHOTO:
        wildlife_show_info();
        return WILDLIFE_INFO;
    case BIRD_NONE:
        return show_message("NO BIRDS YET", "");
    case BIRD_FAILED:
        break;
    }
    return show_message("WILDLIFE ERROR", server_status);
}
