#include "board_wifi.h"

#include <stdio.h>
#include <string.h>

#include "board_status.h"
#include "cyd_update.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "board_wifi";

static volatile bool wifi_connected;
static bool home_assistant_ready;
static TaskHandle_t joined_task;
static uint32_t joined_bit;

bool board_wifi_connected(void)
{
    return wifi_connected;
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
        board_status_set_wifi("joining");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = event_data;
        char text[16];
        if (wifi_connected && home_assistant_ready) {
            board_status_set_ha("waiting for Wi-Fi");
        }
        wifi_connected = false;
        ++attempts;
        board_status_set_wifi("#%u %s", attempts,
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
        board_status_set_wifi(IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Wi-Fi connected; IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        cyd_update_network_up();
        if (joined_task != NULL) {
            xTaskNotify(joined_task, joined_bit, eSetBits);
        }
    }
}

void board_wifi_start(const char *ssid, const char *password, bool ha_ready, TaskHandle_t task,
                      uint32_t joined_bit_value)
{
    home_assistant_ready = ha_ready;
    joined_task = task;
    joined_bit = joined_bit_value;
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
            .threshold.authmode = password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
        },
    };
    memcpy(wifi_config.sta.ssid, ssid, strnlen(ssid, sizeof(wifi_config.sta.ssid)));
    memcpy(wifi_config.sta.password, password,
           strnlen(password, sizeof(wifi_config.sta.password) - 1));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());
}
