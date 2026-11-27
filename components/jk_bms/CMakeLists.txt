idf_component_register(SRCS "src/jk_bms_ble.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp-nimble-cpp
                       PRIV_REQUIRES esp_timer)
