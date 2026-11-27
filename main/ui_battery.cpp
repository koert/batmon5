/*
 * Battery list and battery detail screens, from docs/ui/battery-detail.html (320x240).
 * Unlike the overview these use flex layout; sizes and spacing follow the HTML mock-up.
 */
#include "ui_battery.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {
namespace {

/* Cell voltage range of the cell bars (LiFePO4): empty .. full */
constexpr float kCellBarMinV = 2.80f;
constexpr float kCellBarMaxV = 3.65f;

constexpr uint32_t kColCardPressed = 0x232A33;

using Buf = std::array<char, 24>;

int cell_count(const BatteryData &bat)
{
    int n = 0;
    while (n < 8 && bat.cellVoltages[n] > 0) {
        n++;
    }
    return n;
}

/* Value text and colour for current, power, voltage and temperatures ("--" without data). */
void show_voltage(ValueUnit &v, const BatteryData &bat)
{
    Buf buf;
    if (!bat.hasData()) {
        v.set("--");
        return;
    }
    std::snprintf(buf.data(), buf.size(), "%.2f", bat.voltage);
    v.set(buf.data());
}

void show_current(ValueUnit &v, const BatteryData &bat)
{
    Buf buf;
    if (!bat.hasData()) {
        v.set("--");
        return;
    }
    const float a = std::fabs(bat.current) < 0.005f ? 0.0f : bat.current;
    std::snprintf(buf.data(), buf.size(), a == 0.0f ? "%.2f" : "%+.2f", a);
    v.set(buf.data(), flow_color(a));
}

void show_power(ValueUnit &v, const BatteryData &bat)
{
    Buf buf;
    if (!bat.hasData()) {
        v.set("--");
        return;
    }
    const long w = std::lround(bat.power);
    std::snprintf(buf.data(), buf.size(), w == 0 ? "%ld" : "%+ld", w);
    v.set(buf.data(), flow_color(static_cast<float>(w)));
}

void show_temps(ValueUnit &v, const BatteryData &bat)
{
    Buf buf;
    if (!bat.hasData()) {
        v.set("--");
        return;
    }
    std::snprintf(buf.data(), buf.size(), "%.1f / %.1f", bat.temp1, bat.temp2);
    v.set(buf.data());
}

void show_diff(ValueUnit &v, const BatteryData &bat)
{
    Buf buf;
    if (!bat.hasData() || cell_count(bat) == 0) {
        v.set("--", color::kLabel);
        return;
    }
    std::snprintf(buf.data(), buf.size(), "%.3f", bat.cellDiff);
    v.set(buf.data(), cell_diff_color(bat.cellDiff));
}

int soc_pct(const BatteryData &bat)
{
    return std::clamp(static_cast<int>(std::lround(bat.soc)), 0, 100);
}

/* A row across its parent: children spread from left to right edge. */
lv_obj_t *spread_row_create(lv_obj_t *parent, lv_flex_align_t cross = LV_FLEX_ALIGN_CENTER)
{
    lv_obj_t *row = box_create(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, cross, cross);
    return row;
}

lv_obj_t *back_button_create(lv_obj_t *hdr, const std::function<void()> *cb)
{
    return icon_button_create(
        hdr, LV_SYMBOL_LEFT, 40,
        [](lv_event_t *e) {
            auto *fn = static_cast<const std::function<void()> *>(lv_event_get_user_data(e));
            if (*fn) (*fn)();
        },
        const_cast<std::function<void()> *>(cb));
}

}  // namespace

BatterySlots used_slots(const BatteryData *batteries, uint8_t count)
{
    BatterySlots slots;
    for (uint8_t i = 0; i < count && i < slots.index.size(); i++) {
        if (!batteries[i].bmsName.empty()) {
            slots.index[slots.count++] = i;
        }
    }
    return slots;
}

/* ------------------------------------------------------------------------ */
/* Battery list                                                             */
/* ------------------------------------------------------------------------ */

void BatteryListScreen::create(Callbacks callbacks)
{
    cb_ = std::move(callbacks);
    screen_ = screen_create();

    lv_obj_t *hdr = header_create(screen_);
    back_button_create(hdr, &cb_.back);
    lv_obj_t *title = title_create(hdr, "BATTERIES");
    lv_obj_set_flex_grow(title, 1);
    status_ = text_create(hdr, "", font::kSmall, color::kLabel);
    icon_button_create(
        hdr, LV_SYMBOL_SETTINGS, 40,
        [](lv_event_t *e) {
            auto *self = static_cast<BatteryListScreen *>(lv_event_get_user_data(e));
            if (self->cb_.settings) self->cb_.settings();
        },
        this);

    lv_obj_t *list = box_create(screen_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_hor(list, 6, 0);
    lv_obj_set_style_pad_bottom(list, 6, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scrollable(list, true);  // more than two batteries

    for (uint8_t i = 0; i < cards_.size(); i++) {
        create_card(list, cards_[i], i);
    }

    empty_ = text_create(list, "No batteries.\nTap " LV_SYMBOL_SETTINGS " to select BMS devices.",
                         font::kSmall, color::kLabel);
    lv_obj_set_width(empty_, LV_PCT(100));
    lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(empty_, 60, 0);
}

void BatteryListScreen::create_card(lv_obj_t *parent, Card &c, uint8_t slot)
{
    c.slot = slot;
    c.card = card_create(parent);
    lv_obj_set_size(c.card, LV_PCT(100), 94);
    lv_obj_set_style_pad_ver(c.card, 6, 0);
    lv_obj_set_style_pad_row(c.card, 4, 0);
    lv_obj_set_style_bg_color(c.card, lv_color_hex(kColCardPressed), LV_STATE_PRESSED);
    lv_obj_set_clickable(c.card, true);
    lv_obj_add_event_cb(
        c.card,
        [](lv_event_t *e) {
            auto *self = static_cast<BatteryListScreen *>(lv_event_get_user_data(e));
            lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
            for (const Card &card : self->cards_) {
                if (card.card == target && self->cb_.open) self->cb_.open(card.slot);
            }
        },
        LV_EVENT_CLICKED, this);

    lv_obj_t *line1 = spread_row_create(c.card);
    lv_obj_t *name_box = box_create(line1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(name_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(name_box, 8, 0);
    c.name = text_create(name_box, "");
    c.dot = dot_create(name_box, color::kOffline);
    c.soc.create(line1, "%", font::kUnit, font::kSmall);

    c.bar = level_bar_create(c.card, 6);

    lv_obj_t *line2 = spread_row_create(c.card);
    c.voltage.create(line2, "V");
    c.current.create(line2, "A");
    c.power.create(line2, "W");

    lv_obj_t *line3 = spread_row_create(c.card);
    c.temp.create(line3, "°C");
    lv_obj_t *diff_box = box_create(line3, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(diff_box, 4, 0);
    text_create(diff_box, "Diff", font::kSmall, color::kLabel);
    c.diff.create(diff_box, "V");
}

void BatteryListScreen::update(const BatteryData *batteries, uint8_t count)
{
    int used = 0;
    int connected = 0;
    for (Card &c : cards_) {
        const bool in_use = c.slot < count && !batteries[c.slot].bmsName.empty();
        lv_obj_set_hidden(c.card, !in_use);
        if (!in_use) {
            continue;
        }
        const BatteryData &bat = batteries[c.slot];
        used++;
        connected += bat.connected ? 1 : 0;

        lv_label_set_text(c.name, bat.bmsName.c_str());
        lv_obj_set_style_bg_color(c.dot, lv_color_hex(bat.connected ? color::kOk : color::kOffline), 0);
        if (bat.hasData()) {
            Buf buf;
            std::snprintf(buf.data(), buf.size(), "%d", soc_pct(bat));
            c.soc.set(buf.data());
            lv_bar_set_value(c.bar, soc_pct(bat), LV_ANIM_OFF);
        } else {
            c.soc.set("--", color::kLabel);
            lv_bar_set_value(c.bar, 0, LV_ANIM_OFF);
        }
        show_voltage(c.voltage, bat);
        show_current(c.current, bat);
        show_power(c.power, bat);
        show_temps(c.temp, bat);
        show_diff(c.diff, bat);
    }

    lv_obj_set_hidden(empty_, used > 0);
    Buf buf;
    std::snprintf(buf.data(), buf.size(), "%d connected", connected);
    lv_label_set_text(status_, used > 0 ? buf.data() : "");
}

/* ------------------------------------------------------------------------ */
/* Battery details                                                          */
/* ------------------------------------------------------------------------ */

void BatteryDetailScreen::create(Callbacks callbacks)
{
    cb_ = std::move(callbacks);
    screen_ = screen_create();

    /* Header: back, name, status dot and text, < 1/2 > */
    lv_obj_t *hdr = header_create(screen_);
    back_button_create(hdr, &cb_.back);
    name_ = text_create(hdr, "");
    dot_ = dot_create(hdr, color::kOffline);
    lv_obj_set_style_margin_left(dot_, 8, 0);
    status_ = text_create(hdr, "", font::kSmall, color::kLabel);
    lv_obj_set_style_margin_left(status_, 6, 0);
    lv_obj_set_flex_grow(status_, 1);
    auto page_cb = [](lv_event_t *e) {
        auto *self = static_cast<BatteryDetailScreen *>(lv_event_get_user_data(e));
        lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
        if (self->cb_.page) self->cb_.page(target == self->prev_ ? -1 : +1);
    };
    prev_ = icon_button_create(hdr, LV_SYMBOL_LEFT, 36, page_cb, this);
    pager_ = text_create(hdr, "", font::kSmall, color::kLabel);
    next_ = icon_button_create(hdr, LV_SYMBOL_RIGHT, 36, page_cb, this);

    /* Body: two cards side by side */
    lv_obj_t *body = box_create(screen_, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, 6, 0);
    lv_obj_set_style_pad_bottom(body, 6, 0);
    lv_obj_set_style_pad_column(body, 6, 0);

    /* Left card: SOC, voltage, current, power, temperatures */
    lv_obj_t *left = card_create(body);
    lv_obj_set_size(left, 154, LV_PCT(100));

    lv_obj_t *soc_row = spread_row_create(left, LV_FLEX_ALIGN_END);
    soc_.create(soc_row, "%", font::kSoc, font::kUnit);
    lv_obj_t *soc_title = title_create(soc_row, "SOC");
    lv_obj_set_style_pad_bottom(soc_title, font::kSoc->base_line - font::kSmall->base_line, 0);

    soc_bar_ = level_bar_create(left, 8);
    lv_obj_set_style_margin_top(soc_bar_, 4, 0);
    lv_obj_set_style_margin_bottom(soc_bar_, 10, 0);

    kv_row_create(left, "Voltage", voltage_, "V");
    kv_row_create(left, "Current", current_, "A");
    kv_row_create(left, "Power", power_, "W");
    kv_row_create(left, "Temp", temp_, "°C");

    /* Right card: cell voltages and difference */
    lv_obj_t *right = card_create(body);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_grow(right, 1);

    lv_obj_t *cells_hdr = spread_row_create(right);
    lv_obj_set_style_margin_bottom(cells_hdr, 6, 0);
    title_create(cells_hdr, "CELLS");
    cell_count_ = text_create(cells_hdr, "", font::kSmall, color::kLabel);

    lv_obj_t *cell_list = box_create(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(cell_list, LV_PCT(100));
    lv_obj_set_flex_grow(cell_list, 1);
    lv_obj_set_scrollable(cell_list, true);  // more than 4 cells
    for (int i = 0; i < kMaxCells; i++) {
        CellRow &cell = cells_[i];
        cell.row = box_create(cell_list, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(cell.row, LV_PCT(100), 26);
        lv_obj_set_flex_align(cell.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(cell.row, 6, 0);
        Buf n;
        std::snprintf(n.data(), n.size(), "%d", i + 1);
        lv_obj_t *num = text_create(cell.row, n.data(), font::kSmall, color::kLabel);
        lv_obj_set_width(num, 10);
        cell.bar = level_bar_create(cell.row, 8);
        lv_obj_set_width(cell.bar, 0);
        lv_obj_set_flex_grow(cell.bar, 1);
        cell.value = text_create(cell.row, "--");
        lv_obj_set_width(cell.value, 46);
        lv_obj_set_style_text_align(cell.value, LV_TEXT_ALIGN_RIGHT, 0);
    }

    kv_row_create(right, "Diff", diff_, "V");
}

void BatteryDetailScreen::update(const BatteryData *batteries, uint8_t count, uint8_t slot)
{
    if (slot >= count) {
        return;
    }
    const BatteryData &bat = batteries[slot];
    const BatterySlots used = used_slots(batteries, count);
    int pos = 0;
    for (int i = 0; i < used.count; i++) {
        if (used.index[i] == slot) pos = i;
    }

    /* Header */
    lv_label_set_text(name_, bat.bmsName.empty() ? "--" : bat.bmsName.c_str());
    const bool data = bat.hasData();
    lv_obj_set_style_bg_color(dot_, lv_color_hex(bat.connected ? color::kOk
                                                 : data        ? color::kWarn
                                                               : color::kOffline), 0);
    lv_label_set_text(status_, bat.connected ? "Connected" : data ? "Reconnecting..." : "Searching...");
    Buf buf;
    std::snprintf(buf.data(), buf.size(), "%d/%d", pos + 1, std::max<int>(used.count, 1));
    lv_label_set_text(pager_, buf.data());
    lv_obj_set_disabled(prev_, pos <= 0);
    lv_obj_set_disabled(next_, pos >= used.count - 1);

    /* Left card */
    if (data) {
        std::snprintf(buf.data(), buf.size(), "%d", soc_pct(bat));
        soc_.set(buf.data());
        lv_bar_set_value(soc_bar_, soc_pct(bat), LV_ANIM_OFF);
    } else {
        soc_.set("--", color::kLabel);
        lv_bar_set_value(soc_bar_, 0, LV_ANIM_OFF);
    }
    show_voltage(voltage_, bat);
    show_current(current_, bat);
    show_power(power_, bat);
    show_temps(temp_, bat);

    /* Cells: the lowest cell takes the warning colour when the difference is too large */
    const int n = data ? cell_count(bat) : 0;
    float min_v = 99.0f;
    for (int i = 0; i < n; i++) {
        min_v = std::min(min_v, bat.cellVoltages[i]);
    }
    const bool unbalanced = n > 0 && bat.cellDiff >= kCellDiffWarn;
    const uint32_t diff_color = cell_diff_color(bat.cellDiff);

    if (n > 0) {
        std::snprintf(buf.data(), buf.size(), "%dS", n);
        lv_label_set_text(cell_count_, buf.data());
    } else {
        lv_label_set_text(cell_count_, "");
    }
    for (int i = 0; i < kMaxCells; i++) {
        CellRow &cell = cells_[i];
        lv_obj_set_hidden(cell.row, i >= std::max(n, 4));  // 4 placeholder rows without data
        if (i >= n) {
            lv_bar_set_value(cell.bar, 0, LV_ANIM_OFF);
            lv_label_set_text(cell.value, "--");
            set_text_color(cell.value, color::kLabel);
            continue;
        }
        const float v = bat.cellVoltages[i];
        const bool low = unbalanced && v == min_v;
        const int pct = std::clamp(
            static_cast<int>(std::lround((v - kCellBarMinV) / (kCellBarMaxV - kCellBarMinV) * 100)), 0, 100);
        lv_bar_set_value(cell.bar, pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(cell.bar, lv_color_hex(low ? diff_color : color::kOk),
                                  LV_PART_INDICATOR);
        std::snprintf(buf.data(), buf.size(), "%.3f", v);
        lv_label_set_text(cell.value, buf.data());
        set_text_color(cell.value, low ? diff_color : color::kValue);
    }
    show_diff(diff_, bat);
}

}  // namespace ui
