#pragma once

/*
 * Overview screen (design: docs/ui/overview.svg).
 *
 * All functions take the LVGL port lock themselves, so they can be called
 * from any task. Unknown values are shown as "--": pass NAN for floats,
 * a negative number for percentages, or NULL for the solar status.
 */

/** Build the overview screen on the active display. Call once after display_init(). */
void ui_overview_create(void);

/**
 * @param soc_pct   state of charge, 0..100
 * @param power_w   battery power, positive = charging, negative = discharging
 * @param voltage_v battery voltage
 */
void ui_overview_set_battery(int soc_pct, float power_w, float voltage_v);

/**
 * @param power_w solar (PV) power
 * @param status  charger state text, e.g. "Off", "Bulk", "Absorption", "Float", "Fault"
 */
void ui_overview_set_solar(float power_w, const char *status);

/**
 * @param fresh_pct drinking water tank level, 0..100
 * @param grey_pct  grey water tank level, 0..100
 */
void ui_overview_set_water(int fresh_pct, int grey_pct);
