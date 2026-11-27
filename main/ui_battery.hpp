#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "lvgl.h"

#include "jk_bms/battery_data.hpp"
#include "jk_bms/jk_bms_ble.hpp"
#include "ui_common.hpp"

namespace ui {

/** Index of every battery slot in use (a BMS is assigned: non-empty name), in slot order. */
struct BatterySlots {
    std::array<uint8_t, JKBmsBle::kMaxBatteries> index{};
    uint8_t count = 0;
};
BatterySlots used_slots(const BatteryData *batteries, uint8_t count);

/**
 * Battery list (docs/ui/battery-detail.html, screen 1): one card per BMS.
 * Methods must be called with the LVGL lock held.
 */
class BatteryListScreen {
public:
    struct Callbacks {
        std::function<void()> back;          // to the overview
        std::function<void()> settings;      // gear: BMS selection
        std::function<void(uint8_t)> open;   // battery slot tapped: details
    };

    void create(Callbacks callbacks);
    lv_obj_t *screen() const { return screen_; }
    void update(const BatteryData *batteries, uint8_t count);

private:
    struct Card {
        lv_obj_t *card = nullptr;
        lv_obj_t *name = nullptr;
        lv_obj_t *dot = nullptr;
        lv_obj_t *bar = nullptr;
        ValueUnit soc, voltage, current, power, temp, diff;
        uint8_t slot = 0;
    };

    void create_card(lv_obj_t *parent, Card &card, uint8_t slot);

    Callbacks cb_;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *empty_ = nullptr;
    std::array<Card, JKBmsBle::kMaxBatteries> cards_;
};

/**
 * Details of one battery (docs/ui/battery-detail.html, screens 2-4).
 * Methods must be called with the LVGL lock held.
 */
class BatteryDetailScreen {
public:
    struct Callbacks {
        std::function<void()> back;          // to the battery list
        std::function<void(int)> page;       // -1 = previous, +1 = next battery
    };

    void create(Callbacks callbacks);
    lv_obj_t *screen() const { return screen_; }

    /** Show battery slot `slot`. */
    void update(const BatteryData *batteries, uint8_t count, uint8_t slot);

private:
    static constexpr int kMaxCells = BatteryData::kMaxCells;
    struct CellRow {
        lv_obj_t *row = nullptr;
        lv_obj_t *bar = nullptr;
        lv_obj_t *value = nullptr;
    };

    Callbacks cb_;
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *name_ = nullptr;
    lv_obj_t *dot_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *prev_ = nullptr;
    lv_obj_t *next_ = nullptr;
    lv_obj_t *pager_ = nullptr;
    lv_obj_t *soc_bar_ = nullptr;
    lv_obj_t *cell_count_ = nullptr;
    lv_obj_t *cell_list_ = nullptr;   // scrolls when the cells don't fit
    ValueUnit soc_, voltage_, current_, power_, temp_, diff_;
    std::array<CellRow, kMaxCells> cells_;
};

}  // namespace ui
