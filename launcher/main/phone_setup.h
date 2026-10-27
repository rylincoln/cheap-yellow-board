/* Phone setup: while it runs, the board is also an open Wi-Fi access point
   with a captive web page for the network, its password, the wildlife
   camera server, Home Assistant and the update key, so a phone can do what
   the on-screen keyboard does. */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_wifi.h"

/* The home dashboard's five sensors, then the weather dashboard's seven. */
#define PHONE_HA_SENSOR_COUNT 12

/* What the page submits, and what it shows when drawn again. */
typedef struct {
    char ssid[33];
    char password[65];
    char wildlife_url[128];
    char ha_url[128];
    char ha_token[256];
    char ha_sensors[PHONE_HA_SENSOR_COUNT][96];
    char update_key[40];
} phone_settings_t;

/* The NVS key of each sensor field, in page order. */
extern const char *const PHONE_HA_SENSOR_KEYS[PHONE_HA_SENSOR_COUNT];

/* Stores what the page submitted, on the web server's task. The password is
   empty when the phone left the field blank, and so is the token. */
typedef bool (*phone_setup_save_fn)(const phone_settings_t *settings);

typedef struct {
    const wifi_ap_record_t *networks; /* offered on the page, in this order */
    int network_count;
    /* Shown on the page; the password, token and update key are ignored. */
    const phone_settings_t *saved;
    bool ha_token_saved;           /* says "blank keeps the saved token" */
    bool update_key_saved;         /* says "blank keeps the saved key" */
    phone_setup_save_fn save;
} phone_setup_config_t;

/* Starts the access point, a DNS server that answers every name with the
   board's address, and the page. Wi-Fi must be started in station mode. */
esp_err_t phone_setup_start(const phone_setup_config_t *config);

/* Stops all three and returns to station mode. */
void phone_setup_stop(void);

/* "CYD-Setup-" and the last four hex digits of the access point's address. */
const char *phone_setup_network_name(void);

/* The page's address, such as "192.168.4.1". */
const char *phone_setup_address(void);

/* How many phones are joined to the access point. */
int phone_setup_phone_count(void);

/* True once a phone has saved settings since phone_setup_start. */
bool phone_setup_saved(void);
