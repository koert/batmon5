idf_component_register(SRCS "batmon5.cpp" "bms_settings.cpp" "display.cpp"
                            "ui_app.cpp" "ui_battery.cpp" "ui_bms_select.cpp" "ui_common.cpp"
                            "ui_overview.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2c nvs_flash jk_bms)
