idf_component_register(SRCS "wildlife.c" "wildlife_view.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES cyd_display esp_http_client esp_rom ha_glance json_scan mbedtls)
