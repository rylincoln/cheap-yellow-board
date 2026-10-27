idf_component_register(SRCS "main.c" "bigfont_data.c" "glance_layout.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES cyd_display cyd_update esp_timer ha_glance nvs_flash wildlife)
