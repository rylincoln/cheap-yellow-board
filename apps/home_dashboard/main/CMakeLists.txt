idf_component_register(SRCS "main.c" "readings.c" "bigfont.c" "bigfont_data.c" "glance_layout.c"
                            "glance.c" "ha_client.c" "json_scan.c" "wildlife.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES cyd_display esp_event esp_http_client esp_netif
                                     esp_rom esp_timer esp_wifi mbedtls nvs_flash)
