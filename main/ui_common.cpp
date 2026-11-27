#include "ui_common.hpp"

#include <cstring>
#include <initializer_list>

namespace ui {

uint32_t cell_diff_color(float diff_v)
{
    return diff_v >= kCellDiffAlarm ? color::kAlarm
         : diff_v >= kCellDiffWarn  ? color::kWarn
                                    : color::kOk;
}

uint32_t flow_color(float value)
{
    return value > 0 ? color::kOk : value < 0 ? color::kWarn : color::kValue;
}

lv_obj_t *screen_create()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(color::kBg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(scr, font::kSmall, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(color::kValue), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(scr, false);
    return scr;
}

lv_obj_t *box_create(lv_obj_t *parent, lv_flex_flow_t flow)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, flow);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    return box;
}

lv_obj_t *card_create(lv_obj_t *parent)
{
    lv_obj_t *card = box_create(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(card, lv_color_hex(color::kCard), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_ver(card, 8, 0);
    lv_obj_set_style_pad_hor(card, 10, 0);
    return card;
}

lv_obj_t *text_create(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *title_create(lv_obj_t *parent, const char *text)
{
    lv_obj_t *title = text_create(parent, text, font::kSmall, color::kLabel);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    return title;
}

void set_text_color(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

lv_obj_t *header_create(lv_obj_t *parent)
{
    lv_obj_t *hdr = box_create(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(hdr, LV_PCT(100), 40);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return hdr;
}

lv_obj_t *icon_button_create(lv_obj_t *parent, const char *symbol, int32_t w, lv_event_cb_t cb,
                             void *user_data)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, 40);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_clickable(btn, true);
    lv_obj_set_style_text_color(btn, lv_color_hex(color::kLabel), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(color::kValue), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(color::kDisabled), LV_STATE_DISABLED);
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_center(icon);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

lv_obj_t *dot_create(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_clickable(dot, false);
    return dot;
}

lv_obj_t *level_bar_create(lv_obj_t *parent, int32_t height, uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), height);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_clickable(bar, false);
    for (lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, part);
        lv_obj_set_style_radius(bar, height / 2, part);
    }
    lv_obj_set_style_bg_color(bar, lv_color_hex(color::kTrack), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    return bar;
}

void ValueUnit::create(lv_obj_t *parent, const char *unit, const lv_font_t *val_font,
                       const lv_font_t *unit_font)
{
    box_ = box_create(parent, LV_FLEX_FLOW_ROW);
    /* Bottom-aligned labels of different fonts share (roughly) the baseline. */
    lv_obj_set_flex_align(box_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(box_, val_font == unit_font ? 4 : 2, 0);
    value_ = text_create(box_, "--", val_font, color::kValue);
    unit_ = text_create(box_, unit, unit_font, color::kLabel);
    if (val_font != unit_font) {
        /* The unit's descender area is smaller: lift it to the value's baseline. */
        lv_obj_set_style_pad_bottom(unit_, val_font->base_line - unit_font->base_line, 0);
    }
}

void ValueUnit::set(const char *value, uint32_t color)
{
    lv_label_set_text(value_, value);
    set_text_color(value_, color);
    lv_obj_set_hidden(unit_, std::strcmp(value, "--") == 0);
}

lv_obj_t *kv_row_create(lv_obj_t *parent, const char *label, ValueUnit &value, const char *unit)
{
    lv_obj_t *row = box_create(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(row, LV_PCT(100), 26);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(color::kTrack), 0);
    text_create(row, label, font::kSmall, color::kLabel);
    value.create(row, unit);
    return row;
}

}  // namespace ui
