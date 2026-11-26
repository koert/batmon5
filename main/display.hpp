#pragma once

#include "esp_err.h"
#include "lvgl.h"

namespace display {

/**
 * Initialise backlight, ILI9341 panel, FT6336 touch and the LVGL port task.
 * Afterwards, hold an LvglLock for any LVGL calls made from another task.
 */
esp_err_t init(lv_display_t **out_disp = nullptr);

}  // namespace display
