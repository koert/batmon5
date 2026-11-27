#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "lvgl.h"

namespace ui {

/**
 * Overview screen (design: docs/ui/overview.svg).
 *
 * All methods take the LVGL port lock themselves, so they can be called from
 * any task. An empty std::optional (or a null status) is shown as "--".
 */
class OverviewScreen {
public:
    /**
     * Build the screen and show it. Call once after display::init().
     * @param on_battery_click called (in the LVGL task) when the battery card is tapped
     */
    void create(std::function<void()> on_battery_click = {});

    /** The screen object, for lv_screen_load(). */
    lv_obj_t *screen() const { return screen_; }

    /**
     * @param soc_pct   state of charge, 0..100
     * @param power_w   battery power, positive = charging, negative = discharging
     * @param voltage_v battery voltage
     */
    void set_battery(std::optional<int> soc_pct, std::optional<float> power_w,
                     std::optional<float> voltage_v);

    /**
     * @param power_w solar (PV) power
     * @param status  charger state text, e.g. "Off", "Bulk", "Absorption", "Float", "Fault"
     */
    void set_solar(std::optional<float> power_w, const char *status);

    /**
     * @param fresh_pct drinking water tank level, 0..100
     * @param grey_pct  grey water tank level, 0..100
     */
    void set_water(std::optional<int> fresh_pct, std::optional<int> grey_pct);

private:
    enum class Align { Left, Center, Right };

    /* A value with an optional unit in a different font, sharing one baseline,
     * aligned as a group on an anchor x (like SVG text-anchor). */
    class ValueLabel {
    public:
        void create(lv_obj_t *parent, const lv_font_t *val_font, uint32_t val_color,
                    const lv_font_t *unit_font, int32_t gap, int32_t x, int32_t baseline,
                    Align align);
        void set(const char *val, const char *unit = nullptr);
        void set_color(uint32_t color);

    private:
        lv_obj_t *val_ = nullptr;
        lv_obj_t *unit_ = nullptr;  // null if the value has no separate unit
        int32_t x_ = 0;
        int32_t baseline_ = 0;
        int32_t gap_ = 0;
        Align align_ = Align::Left;
    };

    void create_battery_card(lv_obj_t *scr);
    void create_solar_card(lv_obj_t *scr);
    void create_water_card(lv_obj_t *scr);
    static void set_tank(ValueLabel &label, lv_obj_t *bar, std::optional<int> pct);

    lv_obj_t *screen_ = nullptr;
    std::function<void()> on_battery_click_;
    lv_obj_t *soc_arc_ = nullptr;
    lv_obj_t *batt_icon_fill_ = nullptr;
    ValueLabel soc_;
    ValueLabel batt_power_;
    ValueLabel batt_voltage_;
    ValueLabel solar_power_;
    ValueLabel solar_status_;
    ValueLabel fresh_pct_;
    ValueLabel grey_pct_;
    lv_obj_t *fresh_bar_ = nullptr;
    lv_obj_t *grey_bar_ = nullptr;
};

}  // namespace ui
