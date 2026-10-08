idf_component_register(
    SRCS "alarm_app.c" "panel_outputs.c" "panel_display.c" "ui_frame.c" "oled_wire.c"
    INCLUDE_DIRS "."
    REQUIRES alarm_core
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer esp_hw_support
                  freertos log esp_driver_gpio esp_driver_i2c
)
