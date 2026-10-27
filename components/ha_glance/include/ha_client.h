/* Home Assistant's REST API: one template request per refresh. */
#ifndef HA_CLIENT_H
#define HA_CLIENT_H

#include <stddef.h>

#include "esp_err.h"

/* POSTs body to {base_url}/api/template as JSON, with token as the bearer
   credential and a 5 s timeout, and copies the reply, NUL-terminated, into
   reply. A reply longer than capacity - 1 bytes is an error. *http_status
   gets the HTTP status, or 0 when none came back; the result is ESP_OK only
   on HTTP 200. */
esp_err_t ha_fetch(const char *base_url, const char *token, const char *body, char *reply,
                   size_t capacity, int *http_status);

#endif
