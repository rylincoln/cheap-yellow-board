idf_component_register(SRCS "bigfont.c" "readings.c" "glance_tile.c" "glance_draw.c"
                            "ha_client.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES cyd_display esp_http_client esp_timer log mbedtls)
