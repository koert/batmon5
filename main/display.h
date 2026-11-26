#pragma once

#include "esp_err.h"
#include "lvgl.h"

/**
 * Initialise backlight, ILI9341 panel, FT6336 touch and the LVGL port task.
 * Afterwards, wrap any LVGL calls in lvgl_port_lock(0) / lvgl_port_unlock().
 */
esp_err_t display_init(lv_display_t **out_disp);
