#pragma once

#include "esp_lvgl_port.h"

/**
 * Scoped LVGL port lock. LVGL runs in the esp_lvgl_port task; any LVGL call
 * from another task must hold this lock. The underlying mutex is recursive,
 * so nesting is safe.
 */
class LvglLock {
public:
    LvglLock() { lvgl_port_lock(0); }
    ~LvglLock() { lvgl_port_unlock(); }

    LvglLock(const LvglLock &) = delete;
    LvglLock &operator=(const LvglLock &) = delete;
};
