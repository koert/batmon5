idf_component_register(SRCS "batmon5.cpp" "display.cpp" "ui_overview.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2c)
