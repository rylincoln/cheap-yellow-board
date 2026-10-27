/* Joins the configured Wi-Fi network and keeps retrying. */
#ifndef BOARD_WIFI_H
#define BOARD_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Joins and retries for good, reporting with board_status_set_wifi. On each
   address it calls cyd_update_network_up() and notifies task (if not NULL)
   with joined_bit. When ha_ready, a lost connection sets the Home Assistant
   status to "waiting for Wi-Fi". */
void board_wifi_start(const char *ssid, const char *password, bool ha_ready, TaskHandle_t task,
                      uint32_t joined_bit);
bool board_wifi_connected(void);

#endif
