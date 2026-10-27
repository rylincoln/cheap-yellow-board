idf_component_register(SRCS "main.c" "bigfont_data.c" "glance_layout.c" "wildlife.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES cyd_display cyd_update esp_event esp_http_client esp_netif
                                     esp_rom esp_timer esp_wifi ha_glance json_scan mbedtls nvs_flash)
