#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "jk_bms/battery_data.hpp"
#include "jk_bms/bms_device.hpp"
#include "jk_bms/jk_bms_ble.hpp"
#include "ui_battery.hpp"
#include "ui_bms_select.hpp"
#include "ui_overview.hpp"

namespace ui {

/**
 * All screens and the navigation between them:
 *
 *   overview --tap battery card--> battery list --tap card--> battery details (< > pages)
 *                                       |
 *                                       +--gear--> BMS selection --Save/back--> battery list
 */
class App {
public:
    /** Called in the LVGL task; they must not block. */
    struct Callbacks {
        std::function<std::vector<BmsDeviceId>()> monitored;  // the current BMS selection
        std::function<void()> start_discovery;
        std::function<void(const std::vector<BmsDeviceId> &)> save_selection;
    };

    /** Build all screens and show the overview. Call once after display::init(). */
    void create(Callbacks callbacks);

    OverviewScreen &overview() { return overview_; }

    /**
     * Pass the latest BMS data and refresh the visible screen. Call periodically from the
     * main task; takes the LVGL lock.
     */
    void update(const BatteryData *batteries, uint8_t count, const std::vector<FoundBms> &found,
                bool scanning);

private:
    enum class Page { Overview, List, Detail, Select };

    void show(Page page);
    void refresh();  // the visible screen, LVGL lock held
    void page_detail(int step);

    Callbacks cb_;
    OverviewScreen overview_;
    BatteryListScreen list_;
    BatteryDetailScreen detail_;
    BmsSelectScreen select_;

    Page page_ = Page::Overview;
    uint8_t detail_slot_ = 0;

    /* Latest data from update(), guarded by the LVGL lock */
    std::array<BatteryData, JKBmsBle::kMaxBatteries> batteries_;
    uint8_t count_ = 0;
    std::vector<FoundBms> found_;
    bool scanning_ = false;
};

}  // namespace ui
