#include "board_status.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#define STATUS_SIZE BOARD_STATUS_SIZE
#define STATUS_MAGIC 0xB1DD5747u

static const char *TAG = "board_status";

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
static volatile uint32_t status_changes;
static uint32_t shown_status_changes;

void board_status_set_stage(const char *stage)
{
    crash_record.magic = STATUS_MAGIC;
    snprintf(crash_record.stage, sizeof(crash_record.stage), "%s", stage);
}

/* Records why this start happened, and what a crashed run was doing. */
void board_status_note_start(void)
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
    board_status_set_stage("starting");
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

void board_status_set_wifi(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    set_status(wifi_status, format, arguments);
    va_end(arguments);
}

void board_status_set_ha(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    set_status(ha_status, format, arguments);
    va_end(arguments);
}

bool board_status_changed(void)
{
    return status_changes != shown_status_changes;
}

void board_status_draw(const char *title, const char *ssid, const char *ha_url,
                       const char *hint)
{
    char wifi[STATUS_SIZE];
    char home_assistant[STATUS_SIZE];
    portENTER_CRITICAL(&status_lock);
    memcpy(wifi, wifi_status, sizeof(wifi));
    memcpy(home_assistant, ha_status, sizeof(home_assistant));
    shown_status_changes = status_changes;
    portEXIT_CRITICAL(&status_lock);

    char network[STATUS_SIZE + 16];
    snprintf(network, sizeof(network), "Wi-Fi %.11s", ssid);
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
    char memory[32];
    snprintf(memory, sizeof(memory), "RAM %uK free", (unsigned)(esp_get_free_heap_size() / 1024));
    bool crashed = crash_record.crashes > 0;
    const struct {
        uint16_t color;
        const char *text;
    } rows[CYD_TEXT_ROWS] = {
        {CYD_COLOR_YELLOW, title},
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
}
