/*
 * Overview screen, built from docs/ui/overview.svg (320x240 landscape).
 *
 * Everything is placed on the screen with absolute coordinates taken from the
 * SVG. Text is positioned by its baseline, like SVG <text>, so the numbers in
 * this file can be compared one-to-one with the design.
 */
#include "ui_overview.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_lvgl_port.h"
#include "lvgl.h"

/* ---- Palette (see the SVG header) ---- */
#define COL_BG          0x101418
#define COL_CARD        0x1B2128
#define COL_TRACK       0x2A323C
#define COL_LABEL       0x9AA5B1
#define COL_VALUE       0xFFFFFF
#define COL_OK          0x3DDC84
#define COL_WARN        0xFFB020
#define COL_ALARM       0xFF5A5A
#define COL_SOLAR       0xFFC940
#define COL_FRESH       0x3B9EFF
#define COL_GREY_WATER  0xB39B77

/* ---- Thresholds ---- */
#define SOC_ALARM_PCT    20     /* SOC below this: red */
#define SOC_WARN_PCT     40     /* SOC below this: amber */
#define GREY_WARN_PCT    75     /* grey water above this: amber */
#define GREY_ALARM_PCT   90     /* grey water above this: red */

#define FONT_SMALL  (&lv_font_montserrat_14)
#define FONT_UNIT   (&lv_font_montserrat_24)
#define FONT_SOC    (&lv_font_montserrat_36)
#define FONT_POWER  (&lv_font_montserrat_40)

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } halign_t;

/* A value with an optional unit in a different font, sharing one baseline,
 * aligned as a group on an anchor x (like SVG text-anchor). */
typedef struct {
    lv_obj_t *val;
    lv_obj_t *unit;     /* NULL if the value has no separate unit */
    int32_t x;
    int32_t baseline;
    int32_t gap;
    halign_t align;
} value_t;

static struct {
    lv_obj_t *soc_arc;
    lv_obj_t *batt_icon_fill;
    value_t soc;
    value_t batt_power;
    value_t batt_voltage;
    value_t solar_power;
    value_t solar_status;
    value_t fresh_pct;
    value_t grey_pct;
    lv_obj_t *fresh_bar;
    lv_obj_t *grey_bar;
} ui;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static int32_t font_ascent(const lv_font_t *font)
{
    return font->line_height - font->base_line;
}

static int32_t text_width(lv_obj_t *label)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    int32_t letter_space = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(label), font, letter_space, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

static lv_obj_t *label_create(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text_static(label, "");
    return label;
}

/* Static text, left-aligned at x with its baseline at y. */
static lv_obj_t *text_at(lv_obj_t *parent, const char *text, const lv_font_t *font,
                         uint32_t color, int32_t x, int32_t baseline)
{
    lv_obj_t *label = label_create(parent, font, color);
    lv_label_set_text_static(label, text);
    lv_obj_set_pos(label, x, baseline - font_ascent(font));
    return label;
}

static lv_obj_t *rect_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                             uint32_t color, int32_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_scrollable(obj, false);
    return obj;
}

static void value_create(value_t *v, lv_obj_t *parent,
                         const lv_font_t *val_font, uint32_t val_color,
                         const lv_font_t *unit_font, int32_t gap,
                         int32_t x, int32_t baseline, halign_t align)
{
    v->val = label_create(parent, val_font, val_color);
    v->unit = unit_font ? label_create(parent, unit_font, COL_LABEL) : NULL;
    v->x = x;
    v->baseline = baseline;
    v->gap = gap;
    v->align = align;
}

static void value_set(value_t *v, const char *val, const char *unit)
{
    lv_label_set_text(v->val, val);
    int32_t total = text_width(v->val);
    if (v->unit) {
        lv_label_set_text(v->unit, unit ? unit : "");
        total += v->gap + text_width(v->unit);
    }

    int32_t left = v->x;
    if (v->align == ALIGN_CENTER) {
        left = v->x - total / 2;
    } else if (v->align == ALIGN_RIGHT) {
        left = v->x - total;
    }

    const lv_font_t *val_font = lv_obj_get_style_text_font(v->val, LV_PART_MAIN);
    lv_obj_set_pos(v->val, left, v->baseline - font_ascent(val_font));
    if (v->unit) {
        const lv_font_t *unit_font = lv_obj_get_style_text_font(v->unit, LV_PART_MAIN);
        lv_obj_set_pos(v->unit, left + text_width(v->val) + v->gap,
                       v->baseline - font_ascent(unit_font));
    }
}

static void value_set_color(value_t *v, uint32_t color)
{
    lv_obj_set_style_text_color(v->val, lv_color_hex(color), 0);
}

static void divider_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    rect_create(parent, x, y, w, 1, COL_TRACK, 0);
}

static lv_obj_t *bar_create(lv_obj_t *parent, int32_t x, int32_t y, uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, 130, 12);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

/* Power as text: below 1000 W in whole watts, above that in kW with one decimal. */
static void format_power(float w, bool show_sign, char *val, size_t len, const char **unit)
{
    *unit = "W";
    if (isnan(w)) {
        snprintf(val, len, "--");
        return;
    }
    const char *fmt_w = show_sign ? "%+d" : "%d";
    const char *fmt_kw = show_sign ? "%+.1f" : "%.1f";
    int rounded = (int)lroundf(w);
    if (rounded == 0) {
        snprintf(val, len, "0");
    } else if (abs(rounded) < 1000) {
        snprintf(val, len, fmt_w, rounded);
    } else {
        snprintf(val, len, fmt_kw, w / 1000.0f);
        *unit = "kW";
    }
}

static int clamp_pct(int pct)
{
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

/* ------------------------------------------------------------------------ */
/* Cards                                                                    */
/* ------------------------------------------------------------------------ */

static void battery_card_create(lv_obj_t *scr)
{
    rect_create(scr, 6, 6, 152, 228, COL_CARD, 8);

    /* Title with battery icon; the icon's fill follows the SOC. */
    lv_obj_t *outline = rect_create(scr, 15, 14, 17, 11, COL_CARD, 2);
    lv_obj_set_style_border_width(outline, 2, 0);
    lv_obj_set_style_border_color(outline, lv_color_hex(COL_LABEL), 0);
    rect_create(scr, 32, 17, 2, 5, COL_LABEL, 0);
    ui.batt_icon_fill = rect_create(scr, 18, 17, 0, 5, COL_OK, 0);

    lv_obj_t *title = text_at(scr, "BATTERY", FONT_SMALL, COL_LABEL, 40, 24);
    lv_obj_set_style_text_letter_space(title, 1, 0);

    /* SOC arc: 270 degrees, open at the bottom; centre (82,86),
     * radius 52 to the middle of the 12 px stroke. */
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_remove_style_all(arc);
    lv_obj_set_pos(arc, 82 - 58, 86 - 58);
    lv_obj_set_size(arc, 116, 116);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_OK), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    ui.soc_arc = arc;

    value_create(&ui.soc, scr, FONT_SOC, COL_VALUE, FONT_UNIT, 0, 82, 99, ALIGN_CENTER);
    lv_obj_t *soc_label = text_at(scr, "SOC", FONT_SMALL, COL_LABEL, 0, 138);
    lv_obj_set_x(soc_label, 82 - text_width(soc_label) / 2);

    divider_create(scr, 16, 148, 132);
    value_create(&ui.batt_power, scr, FONT_POWER, COL_VALUE, FONT_UNIT, 4, 82, 192,
                 ALIGN_CENTER);

    divider_create(scr, 16, 207, 132);
    text_at(scr, "Voltage", FONT_SMALL, COL_LABEL, 16, 225);
    value_create(&ui.batt_voltage, scr, FONT_SMALL, COL_VALUE, FONT_SMALL, 2, 148, 225,
                 ALIGN_RIGHT);
}

static void solar_card_create(lv_obj_t *scr)
{
    rect_create(scr, 164, 6, 150, 112, COL_CARD, 8);

    /* Title with sun icon: disc plus eight short rays around (180,20). */
    static const lv_point_precise_t rays[8][2] = {
        { {180, 13}, {180, 14} }, { {180, 26}, {180, 27} },
        { {173, 20}, {174, 20} }, { {186, 20}, {187, 20} },
        { {175, 15}, {176, 16} }, { {185, 15}, {184, 16} },
        { {175, 25}, {176, 24} }, { {185, 25}, {184, 24} },
    };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *ray = lv_line_create(scr);
        lv_line_set_points(ray, rays[i], 2);
        lv_obj_set_style_line_width(ray, 2, 0);
        lv_obj_set_style_line_color(ray, lv_color_hex(COL_SOLAR), 0);
        lv_obj_set_style_line_rounded(ray, true, 0);
    }
    rect_create(scr, 176, 16, 8, 8, COL_SOLAR, LV_RADIUS_CIRCLE);

    lv_obj_t *title = text_at(scr, "SOLAR", FONT_SMALL, COL_LABEL, 194, 24);
    lv_obj_set_style_text_letter_space(title, 1, 0);

    divider_create(scr, 174, 32, 130);
    value_create(&ui.solar_power, scr, FONT_POWER, COL_VALUE, FONT_UNIT, 4, 239, 76,
                 ALIGN_CENTER);

    divider_create(scr, 174, 91, 130);
    text_at(scr, "Status", FONT_SMALL, COL_LABEL, 174, 109);
    value_create(&ui.solar_status, scr, FONT_SMALL, COL_VALUE, NULL, 0, 304, 109,
                 ALIGN_RIGHT);
}

static void water_card_create(lv_obj_t *scr)
{
    rect_create(scr, 164, 124, 150, 110, COL_CARD, 8);

    /* Title with droplet icon: a circle plus a square turned 45 degrees on top. */
    lv_obj_t *tip = rect_create(scr, 177, 132, 6, 6, COL_FRESH, 0);
    lv_obj_set_style_transform_pivot_x(tip, 3, 0);
    lv_obj_set_style_transform_pivot_y(tip, 3, 0);
    lv_obj_set_style_transform_rotation(tip, 450, 0);
    rect_create(scr, 175, 133, 10, 10, COL_FRESH, LV_RADIUS_CIRCLE);

    lv_obj_t *title = text_at(scr, "WATER", FONT_SMALL, COL_LABEL, 194, 142);
    lv_obj_set_style_text_letter_space(title, 1, 0);

    text_at(scr, "Fresh", FONT_SMALL, COL_LABEL, 174, 166);
    value_create(&ui.fresh_pct, scr, FONT_SMALL, COL_VALUE, NULL, 0, 304, 166, ALIGN_RIGHT);
    ui.fresh_bar = bar_create(scr, 174, 172, COL_FRESH);

    text_at(scr, "Grey", FONT_SMALL, COL_LABEL, 174, 204);
    value_create(&ui.grey_pct, scr, FONT_SMALL, COL_VALUE, NULL, 0, 304, 204, ALIGN_RIGHT);
    ui.grey_bar = bar_create(scr, 174, 210, COL_GREY_WATER);
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

void ui_overview_create(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    battery_card_create(scr);
    solar_card_create(scr);
    water_card_create(scr);

    lvgl_port_unlock();

    /* Start with every value unknown. */
    ui_overview_set_battery(-1, NAN, NAN);
    ui_overview_set_solar(NAN, NULL);
    ui_overview_set_water(-1, -1);
}

void ui_overview_set_battery(int soc_pct, float power_w, float voltage_v)
{
    char buf[16];
    const char *unit;

    lvgl_port_lock(0);

    /* SOC */
    if (soc_pct < 0) {
        lv_arc_set_value(ui.soc_arc, 0);
        lv_obj_set_width(ui.batt_icon_fill, 0);
        value_set(&ui.soc, "--", "%");
    } else {
        int soc = clamp_pct(soc_pct);
        uint32_t color = soc < SOC_ALARM_PCT ? COL_ALARM
                       : soc < SOC_WARN_PCT  ? COL_WARN
                                             : COL_OK;
        lv_arc_set_value(ui.soc_arc, soc);
        lv_obj_set_style_arc_color(ui.soc_arc, lv_color_hex(color), LV_PART_INDICATOR);
        lv_obj_set_width(ui.batt_icon_fill, (11 * soc + 50) / 100);
        lv_obj_set_style_bg_color(ui.batt_icon_fill, lv_color_hex(color), 0);
        snprintf(buf, sizeof(buf), "%d", soc);
        value_set(&ui.soc, buf, "%");
    }

    /* Power: green while charging, amber while discharging. */
    format_power(power_w, true, buf, sizeof(buf), &unit);
    value_set(&ui.batt_power, buf, unit);
    int rounded = isnan(power_w) ? 0 : (int)lroundf(power_w);
    value_set_color(&ui.batt_power, rounded > 0 ? COL_OK : rounded < 0 ? COL_WARN : COL_VALUE);

    /* Voltage */
    if (isnan(voltage_v)) {
        snprintf(buf, sizeof(buf), "--");
    } else {
        snprintf(buf, sizeof(buf), "%.2f", voltage_v);
    }
    value_set(&ui.batt_voltage, buf, "V");

    lvgl_port_unlock();
}

void ui_overview_set_solar(float power_w, const char *status)
{
    char buf[16];
    const char *unit;

    lvgl_port_lock(0);
    format_power(power_w, false, buf, sizeof(buf), &unit);
    value_set(&ui.solar_power, buf, unit);
    value_set(&ui.solar_status, status ? status : "--", NULL);
    lvgl_port_unlock();
}

static void tank_set(value_t *v, lv_obj_t *bar, int pct)
{
    char buf[8];
    if (pct < 0) {
        value_set(v, "--", NULL);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        return;
    }
    pct = clamp_pct(pct);
    snprintf(buf, sizeof(buf), "%d%%", pct);
    value_set(v, buf, NULL);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
}

void ui_overview_set_water(int fresh_pct, int grey_pct)
{
    lvgl_port_lock(0);

    tank_set(&ui.fresh_pct, ui.fresh_bar, fresh_pct);
    tank_set(&ui.grey_pct, ui.grey_bar, grey_pct);

    uint32_t grey_color = grey_pct > GREY_ALARM_PCT ? COL_ALARM
                        : grey_pct > GREY_WARN_PCT  ? COL_WARN
                                                    : COL_GREY_WATER;
    lv_obj_set_style_bg_color(ui.grey_bar, lv_color_hex(grey_color), LV_PART_INDICATOR);

    lvgl_port_unlock();
}
