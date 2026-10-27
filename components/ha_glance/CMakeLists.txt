idf_component_register(SRCS "bigfont.c" "readings.c" "glance_tile.c" "glance_draw.c"
                            "ha_client.c" "board_status.c" "board_wifi.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES cyd_display cyd_update esp_event esp_http_client esp_netif esp_timer esp_wifi
                                  log mbedtls)
