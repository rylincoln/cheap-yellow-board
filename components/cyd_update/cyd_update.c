#include "cyd_update.h"

#include <stdio.h>
#include <string.h>

#include "cyd_display.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "push_server.h"
#include "records.h"
#include "update_internal.h"

/* A new build is kept once it has run this long and drawn its first screen. */
#define GOOD_START_US (60LL * 1000 * 1000)
#define GOOD_START_POLL_MS 1000
#define GOOD_START_STACK 3072
/* Starting the push server waits on lwIP and on mDNS's low-priority task,
   so it runs on a task of its own instead of the event task that calls
   cyd_update_network_up. */
#define PUSH_START_STACK 4096
/* What update_lock asks a second update to wait. */
#define BUSY_SECONDS 30
/* How often update_screen_take looks whether the app has lent the screen. */
#define SCREEN_POLL_MS 20
#define LABEL_SIZE 24

static const char *TAG = "cyd_update";

static char app_label[LABEL_SIZE];
static TaskHandle_t screen_task;
static uint32_t screen_bit;
static volatile bool screen_drawn;
/* Apps start on a screen an update may take, such as a status page. */
static volatile bool interruptible = true;
/* Wi-Fi has had an address since start-up. */
static volatile bool network_up;

/* Whether the push server runs, or a task is starting it. */
typedef enum { PUSH_SERVER_STOPPED, PUSH_SERVER_STARTING, PUSH_SERVER_RUNNING } push_server_state_t;

/* Who has the screen. The app has it until an update asks; the app's
   screen task then lends it and waits in cyd_update_lend_screen until the
   update gives it back. */
typedef enum { SCREEN_WITH_APP, SCREEN_ASKED, SCREEN_LENT } screen_state_t;

/* Guards good_start_pending, updating, screen_state and push_server_state. */
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool good_start_pending;
static bool updating;
static screen_state_t screen_state = SCREEN_WITH_APP;
static push_server_state_t push_server_state = PUSH_SERVER_STOPPED;
static SemaphoreHandle_t screen_returned;
/* The percentage on the UPDATING screen, or -1 before it is drawn. Only
   the update holding the lock touches it. */
static int shown_percent = -1;

static const char *state_name(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW:
        return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "pending verify";
    case ESP_OTA_IMG_VALID:
        return "valid";
    case ESP_OTA_IMG_INVALID:
        return "invalid";
    case ESP_OTA_IMG_ABORTED:
        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
        return "undefined";
    }
    return "unknown";
}

/* A for ota_0, B for ota_1, as the launcher's menu names them. */
static char slot_letter(const esp_partition_t *partition)
{
    return (char)('A' + (partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN));
}

/* Keeps a new build once it has had a good start. Until then ESP-IDF refuses
   to begin another update, and any restart rolls back to the build before. */
static void good_start_task(void *argument)
{
    (void)argument;
    while (esp_timer_get_time() < GOOD_START_US || !screen_drawn) {
        vTaskDelay(pdMS_TO_TICKS(GOOD_START_POLL_MS));
    }
    esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Build marked good");
    } else {
        ESP_LOGE(TAG, "Marking the build good failed: %s", esp_err_to_name(result));
    }
    portENTER_CRITICAL(&state_lock);
    good_start_pending = false;
    portEXIT_CRITICAL(&state_lock);
    vTaskDelete(NULL);
}

esp_err_t cyd_update_init(const cyd_update_config_t *config)
{
    if (config == NULL || config->label == NULL || config->screen_task == NULL ||
        config->screen_bit == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (screen_returned != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    screen_returned = xSemaphoreCreateBinary();
    if (screen_returned == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(app_label, sizeof(app_label), "%s", config->label);
    screen_task = config->screen_task;
    screen_bit = config->screen_bit;

    /* The records live in NVS, which the app has usually started already. */
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "NVS failed to start: %s", esp_err_to_name(result));
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    bool have_state = esp_ota_get_state_partition(running, &state) == ESP_OK;
    ESP_LOGI(TAG, "Running from %s, %s", running->label,
             have_state ? state_name(state) : "no OTA state");
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    if (invalid != NULL) {
        ESP_LOGW(TAG, "A new build failed its start: rolled back from slot %c",
                 slot_letter(invalid));
    }
    records_check_usb();

    if (have_state && state == ESP_OTA_IMG_PENDING_VERIFY) {
        good_start_pending = true;
        if (xTaskCreate(good_start_task, "good_start", GOOD_START_STACK, NULL, 1, NULL) !=
            pdPASS) {
            good_start_pending = false;
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "New build: kept after 60 s of uptime and its first screen");
    }
    return ESP_OK;
}

void cyd_update_screen_drawn(void)
{
    screen_drawn = true;
}

static void set_push_server_state(push_server_state_t state)
{
    portENTER_CRITICAL(&state_lock);
    push_server_state = state;
    portEXIT_CRITICAL(&state_lock);
}

static void push_start_task(void *argument)
{
    (void)argument;
    set_push_server_state(push_server_start() == ESP_OK ? PUSH_SERVER_RUNNING
                                                        : PUSH_SERVER_STOPPED);
    vTaskDelete(NULL);
}

/* The first call starts the push server; a later one retries a start that failed. */
void cyd_update_network_up(void)
{
    network_up = true;
    portENTER_CRITICAL(&state_lock);
    bool start = push_server_state == PUSH_SERVER_STOPPED;
    if (start) {
        push_server_state = PUSH_SERVER_STARTING;
    }
    portEXIT_CRITICAL(&state_lock);
    if (start && xTaskCreate(push_start_task, "push_start", PUSH_START_STACK, NULL, 1, NULL) !=
                     pdPASS) {
        ESP_LOGE(TAG, "The push server could not start: no memory for its task");
        set_push_server_state(PUSH_SERVER_STOPPED);
    }
}

void cyd_update_set_interruptible(bool value)
{
    interruptible = value;
}

const char *update_app_label(void)
{
    return app_label;
}

bool update_app_interruptible(void)
{
    return interruptible;
}

bool update_lock(int *busy_seconds)
{
    int64_t now = esp_timer_get_time();
    int busy = 0;
    portENTER_CRITICAL(&state_lock);
    if (good_start_pending) {
        /* The seconds left, rounded up; at least 1 while the first screen is late. */
        int64_t left_us = GOOD_START_US - now;
        busy = left_us > 0 ? (int)((left_us + 999999) / 1000000) : 1;
    } else if (updating) {
        busy = BUSY_SECONDS;
    } else {
        updating = true;
    }
    portEXIT_CRITICAL(&state_lock);
    if (busy_seconds != NULL) {
        *busy_seconds = busy;
    }
    return busy == 0;
}

/* Hands the screen back to the app, or withdraws a request it hasn't
   answered yet; the app redraws either way. */
static void give_back_screen(void)
{
    portENTER_CRITICAL(&state_lock);
    bool was_lent = screen_state == SCREEN_LENT;
    screen_state = SCREEN_WITH_APP;
    portEXIT_CRITICAL(&state_lock);
    shown_percent = -1;
    if (was_lent) {
        xSemaphoreGive(screen_returned);
    }
}

void update_unlock(void)
{
    give_back_screen();
    portENTER_CRITICAL(&state_lock);
    updating = false;
    portEXIT_CRITICAL(&state_lock);
}

static bool screen_is_lent(void)
{
    portENTER_CRITICAL(&state_lock);
    bool lent = screen_state == SCREEN_LENT;
    portEXIT_CRITICAL(&state_lock);
    return lent;
}

/* Asks once, and keeps the request standing after a timeout, so an update
   that polls finds the screen lent as soon as the app gets to it. */
bool update_screen_take(uint32_t wait_ms)
{
    if (screen_task == NULL) {
        return false;
    }
    bool ask = false;
    portENTER_CRITICAL(&state_lock);
    if (screen_state == SCREEN_WITH_APP) {
        screen_state = SCREEN_ASKED;
        ask = true;
    }
    portEXIT_CRITICAL(&state_lock);
    if (ask) {
        xTaskNotify(screen_task, screen_bit, eSetBits);
    }
    int64_t deadline = esp_timer_get_time() + (int64_t)wait_ms * 1000;
    while (!screen_is_lent()) {
        if (esp_timer_get_time() >= deadline) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(SCREEN_POLL_MS));
    }
    return true;
}

void cyd_update_lend_screen(void)
{
    bool lend = false;
    portENTER_CRITICAL(&state_lock);
    if (screen_state == SCREEN_ASKED) {
        screen_state = SCREEN_LENT;
        lend = true;
    }
    portEXIT_CRITICAL(&state_lock);
    if (lend) {
        xSemaphoreTake(screen_returned, portMAX_DELAY);
    }
}

typedef struct {
    uint16_t color;
    const char *text;
} screen_row_t;

/* Draws every text row, which covers the whole screen; rows without text are blank. */
static void draw_screen(const screen_row_t rows[CYD_TEXT_ROWS])
{
    for (int row = 0; row < CYD_TEXT_ROWS; ++row) {
        cyd_text_row(row, rows[row].color, CYD_COLOR_BLACK,
                     rows[row].text != NULL ? rows[row].text : "");
    }
}

void update_screen_progress(const char *label, const char *source, int percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    if (percent == shown_percent || !screen_is_lent()) {
        return;
    }
    char amount[8];
    snprintf(amount, sizeof(amount), "%d%%", percent);
    if (shown_percent < 0) {
        char from[CYD_TEXT_MAX_COLUMNS + 1];
        snprintf(from, sizeof(from), "from %s", source);
        screen_row_t rows[CYD_TEXT_ROWS] = {
            [0] = {CYD_COLOR_CYAN, "UPDATING"},
            [2] = {CYD_COLOR_WHITE, label},
            [3] = {CYD_COLOR_GRAY, from},
            [4] = {CYD_COLOR_WHITE, amount},
        };
        draw_screen(rows);
    } else {
        cyd_text_row(4, CYD_COLOR_WHITE, CYD_COLOR_BLACK, amount);
    }
    shown_percent = percent;
}

void update_screen_finish(const char *title, const char *detail, uint32_t show_ms, bool restarting)
{
    if (screen_is_lent()) {
        uint16_t color = strcmp(title, UPDATE_TITLE_FAILED) == 0 ? CYD_COLOR_YELLOW : CYD_COLOR_CYAN;
        screen_row_t rows[CYD_TEXT_ROWS] = {[0] = {color, title}};
        char lines[64];
        snprintf(lines, sizeof(lines), "%s", detail != NULL ? detail : "");
        char *line = lines;
        for (int row = 2; row < CYD_TEXT_ROWS && *line != '\0'; ++row) {
            rows[row].color = CYD_COLOR_WHITE;
            rows[row].text = line;
            char *end = strchr(line, '\n');
            if (end == NULL) {
                break;
            }
            *end = '\0';
            line = end + 1;
        }
        draw_screen(rows);
        vTaskDelay(pdMS_TO_TICKS(show_ms));
    }
    if (!restarting) {
        give_back_screen();
    }
}
