idf_component_register(SRCS "push_auth.c" "image_desc.c" "update_rules.c" "cyd_update.c" "records.c"
                            "install.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       PRIV_REQUIRES app_update bootloader_support cyd_display esp_partition esp_system
                                     esp_timer json_scan mbedtls nvs_flash)
