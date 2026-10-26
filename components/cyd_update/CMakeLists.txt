idf_component_register(SRCS "push_auth.c" "image_desc.c" "update_rules.c" "cyd_update.c" "records.c"
                            "install.c" "push_server.c" "github_check.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       PRIV_REQUIRES app_update bootloader_support cyd_display esp_http_client
                                     esp_http_server esp_netif esp_partition esp_system esp_timer
                                     esp_wifi json_scan mbedtls nvs_flash)
