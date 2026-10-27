#include "ha_client.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define HA_TIMEOUT_MS 5000

static const char *TAG = "ha_client";

typedef struct {
    char *data;
    size_t length;
    size_t capacity; /* including the NUL */
    bool overflow;
} ha_reply_t;

/* Appends the body of a 200 reply to the caller's fixed buffer, and notes a
   reply that doesn't fit rather than cutting it short. */
static esp_err_t ha_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0 ||
        esp_http_client_get_status_code(event->client) != 200) {
        return ESP_OK;
    }
    ha_reply_t *reply = event->user_data;
    size_t chunk_length = (size_t)event->data_len;
    if (reply->overflow || chunk_length > reply->capacity - 1 - reply->length) {
        reply->overflow = true;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(reply->data + reply->length, event->data, chunk_length);
    reply->length += chunk_length;
    return ESP_OK;
}

esp_err_t ha_fetch(const char *base_url, const char *token, const char *body, char *reply,
                   size_t capacity, int *http_status)
{
    *http_status = 0;
    if (capacity == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    reply[0] = '\0';
    char url[192];
    char authorization[288];
    if (snprintf(url, sizeof(url), "%s/api/template", base_url) >= (int)sizeof(url) ||
        snprintf(authorization, sizeof(authorization), "Bearer %s", token) >=
            (int)sizeof(authorization)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ha_reply_t buffer = {.data = reply, .capacity = capacity};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HA_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = strncmp(url, "https://", 8) == 0 ? esp_crt_bundle_attach : NULL,
        .event_handler = ha_event_handler,
        .user_data = &buffer,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t result = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (result == ESP_OK) {
        result = esp_http_client_set_header(client, "Authorization", authorization);
    }
    if (result == ESP_OK) {
        result = esp_http_client_set_post_field(client, body, (int)strlen(body));
    }
    if (result == ESP_OK) {
        result = esp_http_client_perform(client);
    }
    /* A 401 without a WWW-Authenticate header ends perform with an error,
       but its status still says what went wrong. */
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    *http_status = status > 0 ? status : 0;
    reply[buffer.length] = '\0';
    if (buffer.overflow) {
        result = ESP_ERR_INVALID_SIZE;
    } else if (*http_status != 0 && *http_status != 200) {
        result = ESP_FAIL;
    } else if (result == ESP_OK && *http_status != 200) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "POST /api/template: HTTP 200, %u bytes", (unsigned)buffer.length);
    } else {
        ESP_LOGE(TAG, "POST /api/template: HTTP %d, %u bytes, %s", *http_status,
                 (unsigned)buffer.length, esp_err_to_name(result));
    }
    return result;
}
