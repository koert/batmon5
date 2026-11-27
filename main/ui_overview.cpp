/*
 * Overview screen, built from docs/ui/overview.svg (320x240 landscape).
 *
 * Everything is placed on the screen with absolute coordinates taken from the
 * SVG. Text is positioned by its baseline, like SVG <text>, so the numbers in
 * this file can be compared one-to-one with the design.
 */
#include "ui_overview.hpp"
#include "lvgl_lock.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace ui {
namespace {

/* ---- Palette (see the SVG header) ---- */
constexpr uint32_t kColBg        = 0x101418;
constexpr uint32_t kColCard      = 0x1B2128;
constexpr uint32_t kColTrack     = 0x2A323C;
constexpr uint32_t kColLabel     = 0x9AA5B1;
constexpr uint32_t kColValue     = 0xFFFFFF;
constexpr uint32_t kColOk        = 0x3DDC84;
constexpr uint32_t kColWarn      = 0xFFB020;
constexpr uint32_t kColAlarm     = 0xFF5A5A;
constexpr uint32_t kColSolar     = 0xFFC940;
constexpr uint32_t kColFresh     = 0x3B9EFF;
constexpr uint32_t kColGreyWater = 0xB39B77;

/* ---- Thresholds ---- */
constexpr int kSocAlarmPct  = 20;   // SOC below this: red
constexpr int kSocWarnPct   = 40;   // SOC below this: amber
constexpr int kGreyWarnPct  = 75;   // grey water above this: amber
constexpr int kGreyAlarmPct = 90;   // grey water above this: red

constexpr const lv_font_t *kFontSmall = &lv_font_montserrat_14;
constexpr const lv_font_t *kFontUnit  = &lv_font_montserrat_24;
constexpr const lv_font_t *kFontSoc   = &lv_font_montserrat_36;
constexpr const lv_font_t *kFontPower = &lv_font_montserrat_40;

int32_t font_ascent(const lv_font_t *font)
{
    return font->line_height - font->base_line;
}

const lv_font_t *font_of(lv_obj_t *label)
{
    return lv_obj_get_style_text_font(label, LV_PART_MAIN);
}

int32_t text_width(lv_obj_t *label)
{
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(label), font_of(label),
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

lv_obj_t *label_create(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text_static(label, "");
    return label;
}

/* Static text, left-aligned at x with its baseline at y. */
lv_obj_t *text_at(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color,
                  int32_t x, int32_t baseline)
{
    lv_obj_t *label = label_create(parent, font, color);
    lv_label_set_text_static(label, text);
    lv_obj_set_pos(label, x, baseline - font_ascent(font));
    return label;
}

/* Card title: letter-spaced small caps text. */
void title_at(lv_obj_t *parent, const char *text, int32_t x, int32_t baseline)
{
    lv_obj_t *title = text_at(parent, text, kFontSmall, kColLabel, x, baseline);
    lv_obj_set_style_text_letter_space(title, 1, 0);
}

lv_obj_t *rect_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                      uint32_t color, int32_t radius = 0)
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

void divider_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    rect_create(parent, x, y, w, 1, kColTrack);
}

lv_obj_t *bar_create(lv_obj_t *parent, int32_t x, int32_t y, uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, 130, 12);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kColTrack), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

struct PowerText {
    std::array<char, 16> value;
    const char *unit;
};

/* Power as text: below 1000 W in whole watts, above that in kW with one decimal. */
PowerText format_power(std::optional<float> w, bool show_sign)
{
    PowerText out{};
    out.unit = "W";
    if (!w) {
        std::snprintf(out.value.data(), out.value.size(), "--");
        return out;
    }
    const long rounded = std::lround(*w);
    if (rounded == 0) {
        std::snprintf(out.value.data(), out.value.size(), "0");
    } else if (std::labs(rounded) < 1000) {
        std::snprintf(out.value.data(), out.value.size(), show_sign ? "%+ld" : "%ld", rounded);
    } else {
        std::snprintf(out.value.data(), out.value.size(), show_sign ? "%+.1f" : "%.1f",
                      *w / 1000.0f);
        out.unit = "kW";
    }
    return out;
}

int clamp_pct(int pct)
{
    return std::clamp(pct, 0, 100);
}

}  // namespace

/* ------------------------------------------------------------------------ */
/* ValueLabel                                                               */
/* ------------------------------------------------------------------------ */

void OverviewScreen::ValueLabel::create(lv_obj_t *parent, const lv_font_t *val_font,
                                        uint32_t val_color, const lv_font_t *unit_font,
                                        int32_t gap, int32_t x, int32_t baseline, Align align)
{
    val_ = label_create(parent, val_font, val_color);
    unit_ = unit_font ? label_create(parent, unit_font, kColLabel) : nullptr;
    x_ = x;
    baseline_ = baseline;
    gap_ = gap;
    align_ = align;
}

void OverviewScreen::ValueLabel::set(const char *val, const char *unit)
{
    lv_label_set_text(val_, val);
    const int32_t val_width = text_width(val_);
    int32_t total = val_width;
    if (unit_) {
        lv_label_set_text(unit_, unit ? unit : "");
        total += gap_ + text_width(unit_);
    }

    int32_t left = x_;
    if (align_ == Align::Center) {
        left = x_ - total / 2;
    } else if (align_ == Align::Right) {
        left = x_ - total;
    }

    lv_obj_set_pos(val_, left, baseline_ - font_ascent(font_of(val_)));
    if (unit_) {
        lv_obj_set_pos(unit_, left + val_width + gap_, baseline_ - font_ascent(font_of(unit_)));
    }
}

void OverviewScreen::ValueLabel::set_color(uint32_t color)
{
    lv_obj_set_style_text_color(val_, lv_color_hex(color), 0);
}

/* ------------------------------------------------------------------------ */
/* Cards                                                                    */
/* ------------------------------------------------------------------------ */

void OverviewScreen::create_battery_card(lv_obj_t *scr)
{
    rect_create(scr, 6, 6, 152, 228, kColCard, 8);

    /* Title with battery icon; the icon's fill follows the SOC. */
    lv_obj_t *outline = rect_create(scr, 15, 14, 17, 11, kColCard, 2);
    lv_obj_set_style_border_width(outline, 2, 0);
    lv_obj_set_style_border_color(outline, lv_color_hex(kColLabel), 0);
    rect_create(scr, 32, 17, 2, 5, kColLabel);
    batt_icon_fill_ = rect_create(scr, 18, 17, 0, 5, kColOk);
    title_at(scr, "BATTERY", 40, 24);

    /* SOC arc: 270 degrees, open at the bottom; centre (82,86),
     * radius 52 to the middle of the 10 px stroke. */
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_remove_style_all(arc);
    lv_obj_set_pos(arc, 82 - 57, 86 - 57);
    lv_obj_set_size(arc, 114, 114);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_set_clickable(arc, false);
    for (lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
        lv_obj_set_style_arc_width(arc, 10, part);
        lv_obj_set_style_arc_rounded(arc, true, part);
    }
    lv_obj_set_style_arc_color(arc, lv_color_hex(kColTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(kColOk), LV_PART_INDICATOR);
    soc_arc_ = arc;

    soc_.create(scr, kFontSoc, kColValue, kFontUnit, 0, 82, 99, Align::Center);
    lv_obj_t *soc_label = text_at(scr, "SOC", kFontSmall, kColLabel, 0, 138);
    lv_obj_set_x(soc_label, 82 - text_width(soc_label) / 2);

    divider_create(scr, 16, 148, 132);
    batt_power_.create(scr, kFontPower, kColValue, kFontUnit, 4, 82, 192, Align::Center);

    divider_create(scr, 16, 207, 132);
    text_at(scr, "Voltage", kFontSmall, kColLabel, 16, 225);
    batt_voltage_.create(scr, kFontSmall, kColValue, kFontSmall, 2, 148, 225, Align::Right);
}

void OverviewScreen::create_solar_card(lv_obj_t *scr)
{
    rect_create(scr, 164, 6, 150, 112, kColCard, 8);

    /* Title with sun icon: disc plus eight short rays around (180,20). */
    static const lv_point_precise_t rays[8][2] = {
        {{180, 13}, {180, 14}}, {{180, 26}, {180, 27}},
        {{173, 20}, {174, 20}}, {{186, 20}, {187, 20}},
        {{175, 15}, {176, 16}}, {{185, 15}, {184, 16}},
        {{175, 25}, {176, 24}}, {{185, 25}, {184, 24}},
    };
    for (const auto &ray_points : rays) {
        lv_obj_t *ray = lv_line_create(scr);
        lv_line_set_points(ray, ray_points, 2);
        lv_obj_set_style_line_width(ray, 2, 0);
        lv_obj_set_style_line_color(ray, lv_color_hex(kColSolar), 0);
        lv_obj_set_style_line_rounded(ray, true, 0);
    }
    rect_create(scr, 176, 16, 8, 8, kColSolar, LV_RADIUS_CIRCLE);
    title_at(scr, "SOLAR", 194, 24);

    divider_create(scr, 174, 32, 130);
    solar_power_.create(scr, kFontPower, kColValue, kFontUnit, 4, 239, 76, Align::Center);

    divider_create(scr, 174, 91, 130);
    text_at(scr, "Status", kFontSmall, kColLabel, 174, 109);
    solar_status_.create(scr, kFontSmall, kColValue, nullptr, 0, 304, 109, Align::Right);
}

void OverviewScreen::create_water_card(lv_obj_t *scr)
{
    rect_create(scr, 164, 124, 150, 110, kColCard, 8);

    /* Title with droplet icon: a circle plus a square turned 45 degrees on top. */
    lv_obj_t *tip = rect_create(scr, 177, 132, 6, 6, kColFresh);
    lv_obj_set_style_transform_pivot_x(tip, 3, 0);
    lv_obj_set_style_transform_pivot_y(tip, 3, 0);
    lv_obj_set_style_transform_rotation(tip, 450, 0);
    rect_create(scr, 175, 133, 10, 10, kColFresh, LV_RADIUS_CIRCLE);
    title_at(scr, "WATER", 194, 142);

    text_at(scr, "Fresh", kFontSmall, kColLabel, 174, 166);
    fresh_pct_.create(scr, kFontSmall, kColValue, nullptr, 0, 304, 166, Align::Right);
    fresh_bar_ = bar_create(scr, 174, 172, kColFresh);

    text_at(scr, "Grey", kFontSmall, kColLabel, 174, 204);
    grey_pct_.create(scr, kFontSmall, kColValue, nullptr, 0, 304, 204, Align::Right);
    grey_bar_ = bar_create(scr, 174, 210, kColGreyWater);
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

void OverviewScreen::create(std::function<void()> on_battery_click)
{
    on_battery_click_ = std::move(on_battery_click);
    {
        LvglLock lock;
        lv_obj_t *scr = lv_obj_create(nullptr);
        screen_ = scr;
        lv_obj_set_style_bg_color(scr, lv_color_hex(kColBg), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(scr, false);

        create_battery_card(scr);
        create_solar_card(scr);
        create_water_card(scr);

        /* Transparent touch area over the battery card, on top of its labels: opens the
         * battery list. Lightens the card a little while pressed. */
        lv_obj_t *touch = rect_create(scr, 6, 6, 152, 228, kColValue, 8);
        lv_obj_set_style_bg_opa(touch, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(touch, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_set_clickable(touch, true);
        lv_obj_add_event_cb(
            touch,
            [](lv_event_t *e) {
                auto *self = static_cast<OverviewScreen *>(lv_event_get_user_data(e));
                if (self->on_battery_click_) {
                    self->on_battery_click_();
                }
            },
            LV_EVENT_CLICKED, this);

        lv_screen_load(scr);
    }

    /* Start with every value unknown. */
    set_battery(std::nullopt, std::nullopt, std::nullopt);
    set_solar(std::nullopt, nullptr);
    set_water(std::nullopt, std::nullopt);
}

void OverviewScreen::set_battery(std::optional<int> soc_pct, std::optional<float> power_w,
                                 std::optional<float> voltage_v)
{
    std::array<char, 16> buf;
    LvglLock lock;

    /* SOC */
    if (!soc_pct) {
        lv_arc_set_value(soc_arc_, 0);
        lv_obj_set_width(batt_icon_fill_, 0);
        soc_.set("--", "%");
    } else {
        const int soc = clamp_pct(*soc_pct);
        const uint32_t color = soc < kSocAlarmPct ? kColAlarm
                             : soc < kSocWarnPct  ? kColWarn
                                                  : kColOk;
        lv_arc_set_value(soc_arc_, soc);
        lv_obj_set_style_arc_color(soc_arc_, lv_color_hex(color), LV_PART_INDICATOR);
        lv_obj_set_width(batt_icon_fill_, (11 * soc + 50) / 100);
        lv_obj_set_style_bg_color(batt_icon_fill_, lv_color_hex(color), 0);
        std::snprintf(buf.data(), buf.size(), "%d", soc);
        soc_.set(buf.data(), "%");
    }

    /* Power: green while charging, amber while discharging. */
    const PowerText power = format_power(power_w, true);
    batt_power_.set(power.value.data(), power.unit);
    const long rounded = power_w ? std::lround(*power_w) : 0;
    batt_power_.set_color(rounded > 0 ? kColOk : rounded < 0 ? kColWarn : kColValue);

    /* Voltage */
    if (voltage_v) {
        std::snprintf(buf.data(), buf.size(), "%.2f", *voltage_v);
    } else {
        std::snprintf(buf.data(), buf.size(), "--");
    }
    batt_voltage_.set(buf.data(), "V");
}

void OverviewScreen::set_solar(std::optional<float> power_w, const char *status)
{
    LvglLock lock;
    const PowerText power = format_power(power_w, false);
    solar_power_.set(power.value.data(), power.unit);
    solar_status_.set(status ? status : "--");
}

void OverviewScreen::set_tank(ValueLabel &label, lv_obj_t *bar, std::optional<int> pct)
{
    if (!pct) {
        label.set("--");
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        return;
    }
    const int value = clamp_pct(*pct);
    std::array<char, 8> buf;
    std::snprintf(buf.data(), buf.size(), "%d%%", value);
    label.set(buf.data());
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
}

void OverviewScreen::set_water(std::optional<int> fresh_pct, std::optional<int> grey_pct)
{
    LvglLock lock;
    set_tank(fresh_pct_, fresh_bar_, fresh_pct);
    set_tank(grey_pct_, grey_bar_, grey_pct);

    const int grey = grey_pct.value_or(0);
    const uint32_t grey_color = grey > kGreyAlarmPct ? kColAlarm
                              : grey > kGreyWarnPct  ? kColWarn
                                                     : kColGreyWater;
    lv_obj_set_style_bg_color(grey_bar_, lv_color_hex(grey_color), LV_PART_INDICATOR);
}

}  // namespace ui
