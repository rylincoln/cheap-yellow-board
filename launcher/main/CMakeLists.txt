idf_component_register(SRCS "main.c" "phone_setup.c" "form_check.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES app_update cyd_display esp_event esp_http_client
                                     esp_http_server esp_netif esp_partition esp_timer
                                     esp_wifi lwip mbedtls nvs_flash)
