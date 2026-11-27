/*
 * BMS selection screen, from docs/ui/battery-detail.html (screens 5 and 6).
 */
#include "ui_bms_select.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "jk_bms/jk_bms_ble.hpp"
#include "ui_common.hpp"

namespace ui {
namespace {

constexpr uint32_t kInRangeMs = 60000;  // a device not seen for this long is "Not found"
constexpr uint8_t kMaxSelected = JKBmsBle::kMaxBatteries;
constexpr uint32_t kColRowPressed = 0x232A33;

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

int signal_level(int8_t rssi)
{
    return rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
}

}  // namespace

void BmsSelectScreen::create(Callbacks callbacks)
{
    cb_ = std::move(callbacks);
    screen_ = screen_create();

    /* Header: back, title, status, refresh */
    lv_obj_t *hdr = header_create(screen_);
    icon_button_create(
        hdr, LV_SYMBOL_LEFT, 40,
        [](lv_event_t *e) {
            auto *self = static_cast<BmsSelectScreen *>(lv_event_get_user_data(e));
            if (self->cb_.back) self->cb_.back();
        },
        this);
    lv_obj_t *title = title_create(hdr, "SELECT BMS");
    lv_obj_set_flex_grow(title, 1);
    status_ = text_create(hdr, "", font::kSmall, color::kLabel);
    refresh_ = icon_button_create(
        hdr, LV_SYMBOL_REFRESH, 40,
        [](lv_event_t *e) {
            auto *self = static_cast<BmsSelectScreen *>(lv_event_get_user_data(e));
            if (self->cb_.scan) self->cb_.scan();
            self->scanning_ = true;
            self->refresh();
        },
        this);

    /* Device list */
    list_ = card_create(screen_);
    lv_obj_set_width(list_, 320 - 2 * 6);
    lv_obj_set_style_margin_left(list_, 6, 0);
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_style_pad_ver(list_, 0, 0);
    lv_obj_set_scrollable(list_, true);
    lv_obj_set_clickable(list_, true);  // scrolling starts from an object that takes the touch
    for (Row &row : rows_) {
        create_row(list_, row);
    }

    /* Footer: "n / 2 selected" and Save */
    lv_obj_t *foot = box_create(screen_, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(foot, LV_PCT(100), 40);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(foot, 16, 0);
    lv_obj_set_style_pad_right(foot, 6, 0);
    count_ = text_create(foot, "", font::kSmall, color::kLabel);

    save_ = box_create(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(save_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(save_, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_pad_hor(save_, 20, 0);
    lv_obj_set_style_radius(save_, 6, 0);
    lv_obj_set_style_bg_opa(save_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(save_, lv_color_hex(color::kOk), 0);
    lv_obj_set_style_bg_color(save_, lv_color_hex(0x2FB86C), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(save_, lv_color_hex(color::kTrack), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(save_, lv_color_hex(color::kBg), 0);
    lv_obj_set_style_text_color(save_, lv_color_hex(color::kOffline), LV_STATE_DISABLED);
    lv_obj_set_clickable(save_, true);
    lv_obj_t *save_label = lv_label_create(save_);
    lv_label_set_text(save_label, "Save");
    lv_obj_add_event_cb(
        save_,
        [](lv_event_t *e) {
            auto *self = static_cast<BmsSelectScreen *>(lv_event_get_user_data(e));
            std::vector<BmsDeviceId> selection;
            for (const Entry &entry : self->entries_) {
                if (entry.selected) selection.push_back(entry.id);
            }
            if (self->cb_.save) self->cb_.save(selection);
        },
        LV_EVENT_CLICKED, this);
}

void BmsSelectScreen::create_row(lv_obj_t *parent, Row &r)
{
    r.row = box_create(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(r.row, LV_PCT(100), 40);
    lv_obj_set_flex_align(r.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r.row, 10, 0);
    lv_obj_set_style_border_side(r.row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(r.row, lv_color_hex(color::kTrack), 0);
    lv_obj_set_style_bg_color(r.row, lv_color_hex(kColRowPressed), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r.row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_clickable(r.row, true);  // the whole row toggles the checkbox
    lv_obj_add_event_cb(
        r.row,
        [](lv_event_t *e) {
            auto *self = static_cast<BmsSelectScreen *>(lv_event_get_user_data(e));
            lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
            for (int i = 0; i < kMaxRows; i++) {
                if (self->rows_[i].row == target) self->toggle(i);
            }
        },
        LV_EVENT_CLICKED, this);

    /* Checkbox */
    r.check = box_create(r.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(r.check, 18, 18);
    lv_obj_set_style_radius(r.check, 4, 0);
    lv_obj_set_style_border_width(r.check, 2, 0);
    lv_obj_set_style_bg_color(r.check, lv_color_hex(color::kOk), 0);
    lv_obj_set_flex_align(r.check, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    r.check_icon = text_create(r.check, LV_SYMBOL_OK, font::kSmall, color::kBg);

    /* Name and address */
    lv_obj_t *info = box_create(r.row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(info, 0);
    lv_obj_set_flex_grow(info, 1);
    r.name = text_create(info, "");
    lv_obj_set_width(r.name, LV_PCT(100));
    lv_label_set_long_mode(r.name, LV_LABEL_LONG_MODE_DOTS);
    r.address = text_create(info, "", font::kSmall, color::kLabel);

    /* Signal and status */
    lv_obj_t *meta = box_create(r.row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t *signal = box_create(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(signal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(signal, 4, 0);
    lv_obj_t *bars = box_create(signal, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(bars, 12);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(bars, 1, 0);
    static constexpr int32_t kBarHeights[kSignalBars] = {4, 7, 10, 12};
    for (int i = 0; i < kSignalBars; i++) {
        lv_obj_t *bar = box_create(bars, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(bar, 3, kBarHeights[i]);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        r.bars[i] = bar;
    }
    r.rssi = text_create(signal, "", font::kSmall, color::kLabel);
    r.status = text_create(meta, "", font::kSmall, color::kLabel);
}

void BmsSelectScreen::open(const std::vector<BmsDeviceId> &monitored)
{
    saved_ = monitored;
    entries_.clear();
    for (const BmsDeviceId &id : monitored) {
        Entry entry;
        entry.id = id;
        entry.saved = true;
        entry.selected = true;
        entries_.push_back(entry);
    }
    scanning_ = true;  // the caller starts a discovery scan
    lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
    refresh();
}

void BmsSelectScreen::update(const std::vector<FoundBms> &found, bool scanning,
                             const BatteryData *batteries, uint8_t count)
{
    scanning_ = scanning;

    for (Entry &entry : entries_) {
        entry.in_range = false;
    }
    for (const FoundBms &dev : found) {
        const bool recent = dev.ageMs < kInRangeMs;
        auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry &e) { return e.id.address == dev.id.address; });
        if (it == entries_.end()) {
            if (!recent || entries_.size() >= kMaxRows) continue;
            Entry entry;
            entry.id = dev.id;
            entries_.push_back(entry);
            it = entries_.end() - 1;
        }
        if (!dev.id.name.empty()) it->id.name = dev.id.name;
        it->in_range = recent;
        it->rssi = dev.rssi;
    }

    /* Saved device i is battery slot i. A connected BMS stops advertising, so the
     * connection state comes from the battery data, not from the scan. */
    for (size_t i = 0; i < entries_.size(); i++) {
        Entry &entry = entries_[i];
        entry.connected = false;
        for (size_t s = 0; s < saved_.size() && s < count; s++) {
            if (saved_[s].address == entry.id.address) entry.connected = batteries[s].connected;
        }
    }
    refresh();
}

int BmsSelectScreen::selected_count() const
{
    return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
                                          [](const Entry &e) { return e.selected; }));
}

bool BmsSelectScreen::changed() const
{
    std::vector<const std::string *> selected;
    for (const Entry &entry : entries_) {
        if (entry.selected) selected.push_back(&entry.id.address);
    }
    if (selected.size() != saved_.size()) return true;
    for (size_t i = 0; i < selected.size(); i++) {
        if (*selected[i] != saved_[i].address) return true;
    }
    return false;
}

void BmsSelectScreen::toggle(int index)
{
    if (index >= static_cast<int>(entries_.size())) return;
    Entry &entry = entries_[index];
    if (entry.selected) {
        entry.selected = false;
    } else if (selected_count() < kMaxSelected) {
        entry.selected = true;
    }
    refresh();
}

void BmsSelectScreen::refresh()
{
    char buf[32];
    const int selected = selected_count();
    const bool full = selected >= kMaxSelected;

    int visible = 0;
    for (int i = 0; i < kMaxRows; i++) {
        Row &r = rows_[i];
        const bool used = i < static_cast<int>(entries_.size());
        lv_obj_set_hidden(r.row, !used);
        if (!used) continue;
        const Entry &e = entries_[i];
        const bool disabled = !e.selected && full;

        lv_obj_set_style_border_width(r.row, visible++ == 0 ? 0 : 1, 0);
        lv_obj_set_disabled(r.row, disabled);

        /* Checkbox */
        lv_obj_set_style_bg_opa(r.check, e.selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(
            r.check, lv_color_hex(e.selected ? color::kOk : disabled ? color::kDisabled : color::kLabel), 0);
        lv_obj_set_hidden(r.check_icon, !e.selected);

        /* Name and address */
        lv_label_set_text(r.name, e.id.name.empty() ? "(no name)" : e.id.name.c_str());
        set_text_color(r.name, disabled ? color::kOffline : color::kValue);
        lv_label_set_text(r.address, upper(e.id.address).c_str());
        set_text_color(r.address, disabled ? color::kOffline : color::kLabel);

        /* Signal */
        const int level = e.in_range ? signal_level(e.rssi) : 0;
        for (int b = 0; b < kSignalBars; b++) {
            lv_obj_set_style_bg_color(r.bars[b],
                                      lv_color_hex(b < level ? color::kLabel : color::kDisabled), 0);
        }
        if (e.in_range) {
            std::snprintf(buf, sizeof(buf), "%d", e.rssi);
            lv_label_set_text(r.rssi, buf);
        } else {
            lv_label_set_text(r.rssi, "--");
        }

        /* Status */
        if (e.connected) {
            lv_label_set_text(r.status, "Connected");
            set_text_color(r.status, color::kOk);
        } else if (e.in_range) {
            lv_label_set_text(r.status, "In range");
            set_text_color(r.status, disabled ? color::kOffline : color::kLabel);
        } else {
            lv_label_set_text(r.status, "Not found");
            set_text_color(r.status, e.selected ? color::kWarn : color::kOffline);
        }
    }

    /* Header and footer */
    if (scanning_) {
        lv_label_set_text(status_, "Scanning...");
    } else {
        const int found = static_cast<int>(std::count_if(
            entries_.begin(), entries_.end(), [](const Entry &e) { return e.in_range || e.connected; }));
        std::snprintf(buf, sizeof(buf), "%d found", found);
        lv_label_set_text(status_, buf);
    }
    lv_obj_set_disabled(refresh_, scanning_);
    std::snprintf(buf, sizeof(buf), "%d / %d selected", selected, kMaxSelected);
    lv_label_set_text(count_, buf);
    lv_obj_set_disabled(save_, !changed());
}

}  // namespace ui
