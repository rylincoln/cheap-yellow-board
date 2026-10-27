#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "board_status.h"
#include "board_wifi.h"
#include "cyd_display.h"
#include "cyd_update.h"
#include "glance_draw.h"
#include "glance_layout.h"
#include "ha_client.h"
#include "readings.h"
#include "wildlife_view.h"
#include "esp_log.h"
#include "esp_timer.h"
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
/* Notification bits for the dashboard task. */
#define TAP_BIT BIT0
#define WIFI_JOINED_BIT BIT1
/* An update wants the screen; see cyd_update_lend_screen. */
#define UPDATE_BIT BIT2
/* A tap on the photo shows the bird's details this long, or until the next tap. */
#define INFO_TIMEOUT_US (30LL * 1000 * 1000)
/* The photo, and a message saying why there is none, give way to the glance
   screen after these. */
#define PHOTO_TIMEOUT_US (30LL * 1000 * 1000)
#define MESSAGE_TIMEOUT_US (5LL * 1000 * 1000)

static const char *TAG = "home_dashboard";

static char wifi_ssid[33];
static char wifi_password[65];
/* The wildlife camera gallery's address, without a trailing slash; empty
   until phone setup stores one. */
static char wildlife_url[WILDLIFE_URL_SIZE];
static TaskHandle_t app_task_handle;

/* Home Assistant settings, the request built from them once, its reply,
   and the readings taken from the replies. */
static const char *const HA_SENSOR_KEYS[SENSOR_COUNT] = {
    [SENSOR_UPSTAIRS] = "ha_upstairs",
    [SENSOR_DOWNSTAIRS] = "ha_downstairs",
    [SENSOR_OUTSIDE] = "ha_outside",
    [SENSOR_BATTERY] = "ha_battery",
    [SENSOR_BATTERY_PCT] = "ha_battery_pct",
};
static char ha_url[128];
static char ha_token[256];
static char ha_entity_ids[SENSOR_COUNT][96];
static const char *const entity_ids[SENSOR_COUNT] = {
    [SENSOR_UPSTAIRS] = ha_entity_ids[SENSOR_UPSTAIRS],
    [SENSOR_DOWNSTAIRS] = ha_entity_ids[SENSOR_DOWNSTAIRS],
    [SENSOR_OUTSIDE] = ha_entity_ids[SENSOR_OUTSIDE],
    [SENSOR_BATTERY] = ha_entity_ids[SENSOR_BATTERY],
    [SENSOR_BATTERY_PCT] = ha_entity_ids[SENSOR_BATTERY_PCT],
};
static char ha_body[768];
static char ha_reply[2048];
static reading_t readings[SENSOR_COUNT];
/* The Home Assistant setting that is missing, such as "no HA token", or
   NULL when the request is ready. */
static const char *ha_setup_problem;

/* ---- Status screen ----

   Until the first Home Assistant readings arrive, the screen shows how the
   dashboard is getting on: why it last started (a crash or watchdog reset
   boots straight back into this image, so this is how one shows), the
   Wi-Fi network and its state, the Home Assistant address and the last
   request. */

/* False until the status screen is first drawn. Nothing else is drawn
   while it is the screen, and it never comes back once left. */
static bool status_on_screen;

static void draw_status(void)
{
    const char *hint = ha_setup_problem != NULL ? "use phone setup"
                       : board_wifi_connected() ? ""
                                                : "EN opens launcher";
    board_status_draw("HOME DASHBOARD", wifi_ssid, ha_url, hint);
    status_on_screen = true;
}

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
        (!status_on_screen || board_status_changed())) {
        draw_status();
    }
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
    board_status_set_stage("asking HA");
    board_status_set_ha("asking HA");
    update_status_screen();
    esp_err_t result = ha_fetch(ha_url, ha_token, ha_body, ha_reply, sizeof(ha_reply),
                                &http_status);
    board_status_set_stage("idle");
    if (result != ESP_OK) {
        if (http_status == 401) {
            board_status_set_ha("HA 401 bad token");
        } else if (http_status != 0 && http_status != 200) {
            board_status_set_ha("HA HTTP %d", http_status);
        } else {
            const char *name = esp_err_to_name(result);
            board_status_set_ha("HA %s", strncmp(name, "ESP_ERR_", 8) == 0 ? name + 8 : name);
        }
        update_status_screen();
        return false;
    }
    int found = readings_apply(ha_reply, entity_ids, SENSOR_COUNT, esp_timer_get_time(), readings);
    int asked = 0;
    for (int sensor = 0; sensor < SENSOR_COUNT; ++sensor) {
        asked += ha_entity_ids[sensor][0] != '\0';
    }
    ESP_LOGI(TAG, "Home Assistant has %d of %d sensors", found, asked);
    if (found == 0) {
        board_status_set_ha("no sensors found");
        update_status_screen();
        return false;
    }
    board_status_set_ha("HA OK");
    return true;
}

/* Composes the tiles from the readings and draws them: all of them on a
   cleared screen with full, else only those that changed. */
static void draw_glance(bool full)
{
    static glance_tile_t tiles[GLANCE_TILE_COUNT];
    int64_t now = esp_timer_get_time();
    bool show_charge = ha_entity_ids[SENSOR_BATTERY_PCT][0] != '\0';
    glance_compose(LCD_WIDTH, LCD_HEIGHT, readings, show_charge, now, tiles);
    glance_draw(tiles, GLANCE_TILE_COUNT, full);
    glance_composed_us = now;
}

/* Shows the glance screen, drawn in full. An update from GitHub may take
   the screen from here. */
static void enter_glance(void)
{
    screen = SCREEN_GLANCE;
    cyd_update_set_interruptible(true);
    draw_glance(true);
}

/* Moves to a screen that gives way to the glance screen after timeout_us. */
static void enter_timed_screen(screen_t next, int64_t timeout_us)
{
    screen = next;
    screen_until_us = esp_timer_get_time() + timeout_us;
    cyd_update_set_interruptible(false);
}

/* Answers a tap on the glance screen at once, then shows the latest bird's
   photo, or its details when there is no photo, or says why there is no
   bird. */
static void show_bird_for_tap(void)
{
    /* The readings are gone from here on, though the timed screen starts
       only once the bird is found. */
    cyd_update_set_interruptible(false);
    switch (wildlife_show_for_tap(wildlife_url)) {
    case WILDLIFE_PHOTO:
        enter_timed_screen(SCREEN_PHOTO, PHOTO_TIMEOUT_US);
        break;
    case WILDLIFE_INFO:
        enter_timed_screen(SCREEN_INFO, INFO_TIMEOUT_US);
        break;
    case WILDLIFE_MESSAGE:
        enter_timed_screen(SCREEN_MESSAGE, MESSAGE_TIMEOUT_US);
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
    /* The battery's charge is optional. */
    for (int sensor = 0; sensor < SENSOR_BATTERY_PCT; ++sensor) {
        if (ha_entity_ids[sensor][0] == '\0') {
            return "no sensor IDs";
        }
    }
    if (readings_template_body(entity_ids, SENSOR_COUNT, ha_body, sizeof(ha_body)) < 0) {
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
        board_status_set_ha("%s", ha_setup_problem);
        ESP_LOGW(TAG, "Home Assistant is not set up (%s); use phone setup", ha_setup_problem);
    }
    return have_wifi;
}

/* The earlier of wake_us and deadline_us, counting only a deadline still
   ahead of now. */
static int64_t sooner(int64_t wake_us, int64_t deadline_us, int64_t now)
{
    return deadline_us > now && deadline_us < wake_us ? deadline_us : wake_us;
}

/* Puts the screen back in full after an update has drawn over it. A timed
   screen isn't worth bringing back. */
static void redraw_after_update(void)
{
    switch (screen) {
    case SCREEN_STATUS:
        draw_status();
        break;
    case SCREEN_GLANCE:
        draw_glance(true);
        break;
    default:
        enter_glance();
        break;
    }
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
    update_status_screen();
    cyd_update_screen_drawn();
    while (true) {
        update_status_screen();
        bool timed = screen == SCREEN_PHOTO || screen == SCREEN_INFO || screen == SCREEN_MESSAGE;
        if (timed && esp_timer_get_time() >= screen_until_us) {
            enter_glance();
        }
        if ((screen == SCREEN_STATUS || screen == SCREEN_GLANCE) && ha_setup_problem == NULL &&
            board_wifi_connected() && esp_timer_get_time() >= next_fetch_us) {
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
        /* An update that drew nothing leaves the screen, a bird photo
           say, and any tap alone. */
        if ((events & UPDATE_BIT) && cyd_update_lend_screen()) {
            redraw_after_update();
            /* Taps made on the update's screen are dropped. */
            ulTaskNotifyValueClear(NULL, TAP_BIT);
            events &= ~TAP_BIT;
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
                wildlife_show_info();
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
    board_status_note_start();
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
    const cyd_update_config_t update_config = {
        .label = "dashboard",
        .screen_task = app_task_handle,
        .screen_bit = UPDATE_BIT,
    };
    ESP_ERROR_CHECK(cyd_update_init(&update_config));
    board_wifi_start(wifi_ssid, wifi_password, ha_setup_problem == NULL, app_task_handle,
                     WIFI_JOINED_BIT);
}
