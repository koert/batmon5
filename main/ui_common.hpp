#pragma once

/*
 * Palette, fonts and widget helpers shared by the screens.
 * Design: docs/ui/overview.svg and docs/ui/battery-detail.html.
 * All functions must be called with the LVGL lock held (or from an LVGL event).
 */
#include <cstdint>

#include "lvgl.h"

namespace ui {

namespace color {
inline constexpr uint32_t kBg        = 0x101418;
inline constexpr uint32_t kCard      = 0x1B2128;
inline constexpr uint32_t kTrack     = 0x2A323C;
inline constexpr uint32_t kDisabled  = 0x3A434E;
inline constexpr uint32_t kOffline   = 0x5C6670;
inline constexpr uint32_t kLabel     = 0x9AA5B1;
inline constexpr uint32_t kValue     = 0xFFFFFF;
inline constexpr uint32_t kOk        = 0x3DDC84;
inline constexpr uint32_t kWarn      = 0xFFB020;
inline constexpr uint32_t kAlarm     = 0xFF5A5A;
inline constexpr uint32_t kSolar     = 0xFFC940;
inline constexpr uint32_t kFresh     = 0x3B9EFF;
inline constexpr uint32_t kGreyWater = 0xB39B77;
}  // namespace color

namespace font {
inline constexpr const lv_font_t *kSmall = &lv_font_montserrat_14;
inline constexpr const lv_font_t *kUnit  = &lv_font_montserrat_24;
inline constexpr const lv_font_t *kSoc   = &lv_font_montserrat_36;
inline constexpr const lv_font_t *kPower = &lv_font_montserrat_40;
}  // namespace font

/* Cell voltage difference thresholds (V) */
inline constexpr float kCellDiffWarn  = 0.050f;
inline constexpr float kCellDiffAlarm = 0.100f;

uint32_t cell_diff_color(float diff_v);
/** Green while charging (> 0), amber while discharging (< 0), white at 0. */
uint32_t flow_color(float value);

/** New screen: background colour, flex column, not scrollable. */
lv_obj_t *screen_create();

/** Transparent, non-clickable, non-scrollable flex container sized to its content. */
lv_obj_t *box_create(lv_obj_t *parent, lv_flex_flow_t flow);

/** Card: rounded, card colour, padding 8/10, flex column. */
lv_obj_t *card_create(lv_obj_t *parent);

lv_obj_t *text_create(lv_obj_t *parent, const char *text, const lv_font_t *font = font::kSmall,
                      uint32_t color = color::kValue);

/** Letter-spaced title text (e.g. "CELLS"). */
lv_obj_t *title_create(lv_obj_t *parent, const char *text);

void set_text_color(lv_obj_t *obj, uint32_t color);

/** 40 px high header row across the screen. */
lv_obj_t *header_create(lv_obj_t *parent);

/** Icon button (LV_SYMBOL_*) of w x 40 px; grey, white while pressed, dark when disabled. */
lv_obj_t *icon_button_create(lv_obj_t *parent, const char *symbol, int32_t w, lv_event_cb_t cb,
                             void *user_data);

/** Status dot of 8 px. */
lv_obj_t *dot_create(lv_obj_t *parent, uint32_t color);

/** Horizontal level bar 0..100 with a rounded track and fill. */
lv_obj_t *level_bar_create(lv_obj_t *parent, int32_t height, uint32_t color = color::kOk);

/** A value with a grey unit after it, sharing one baseline. */
class ValueUnit {
public:
    void create(lv_obj_t *parent, const char *unit, const lv_font_t *val_font = font::kSmall,
                const lv_font_t *unit_font = font::kSmall);
    /** Set the text and colour; the unit is hidden when the value is "--". */
    void set(const char *value, uint32_t color = color::kValue);
    lv_obj_t *obj() const { return box_; }

private:
    lv_obj_t *box_ = nullptr;
    lv_obj_t *value_ = nullptr;
    lv_obj_t *unit_ = nullptr;
};

/** A 26 px "label .... value unit" row with a divider line on top. */
lv_obj_t *kv_row_create(lv_obj_t *parent, const char *label, ValueUnit &value, const char *unit);

}  // namespace ui
