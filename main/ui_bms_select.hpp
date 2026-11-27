#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "lvgl.h"

#include "jk_bms/battery_data.hpp"
#include "jk_bms/bms_device.hpp"

namespace ui {

/**
 * BMS selection (docs/ui/battery-detail.html, screens 5-6): every JK BMS found by the scan, with a
 * checkbox per device. Save hands the checked devices to the `save` callback.
 * Methods must be called with the LVGL lock held.
 */
class BmsSelectScreen {
public:
    struct Callbacks {
        std::function<void()> back;  // cancel
        std::function<void()> scan;  // start a discovery scan
        std::function<void(const std::vector<BmsDeviceId> &)> save;
    };

    void create(Callbacks callbacks);
    lv_obj_t *screen() const { return screen_; }

    /** Start editing; `monitored` is the current selection (listed first, checked). */
    void open(const std::vector<BmsDeviceId> &monitored);

    /** New scan results; batteries[i] belongs to monitored[i] (for the connection state). */
    void update(const std::vector<FoundBms> &found, bool scanning, const BatteryData *batteries,
                uint8_t count);

private:
    static constexpr int kMaxRows = 12;
    static constexpr int kSignalBars = 4;

    struct Entry {
        BmsDeviceId id;
        bool saved = false;      // part of the current selection
        bool selected = false;   // checked
        bool connected = false;
        bool in_range = false;   // seen recently by the scan
        int8_t rssi = 0;
    };
    struct Row {
        lv_obj_t *row = nullptr;
        lv_obj_t *check = nullptr;
        lv_obj_t *check_icon = nullptr;
        lv_obj_t *name = nullptr;
        lv_obj_t *address = nullptr;
        std::array<lv_obj_t *, kSignalBars> bars{};
        lv_obj_t *rssi = nullptr;
        lv_obj_t *status = nullptr;
    };

    void create_row(lv_obj_t *parent, Row &row);
    void toggle(int index);
    void refresh();
    int selected_count() const;
    bool changed() const;

    Callbacks cb_;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *refresh_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *count_ = nullptr;
    lv_obj_t *save_ = nullptr;
    std::array<Row, kMaxRows> rows_;
    std::vector<Entry> entries_;
    std::vector<BmsDeviceId> saved_;
    bool scanning_ = false;
};

}  // namespace ui
