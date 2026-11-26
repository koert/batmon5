idf_component_register(SRCS "batmon5.c" "display.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2c)
