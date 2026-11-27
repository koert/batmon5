#include "ui_app.hpp"

#include <algorithm>

#include "lvgl_lock.hpp"

namespace ui {

void App::create(Callbacks callbacks)
{
    cb_ = std::move(callbacks);

    /* The overview takes the lock itself and loads its screen. */
    overview_.create([this] { show(Page::List); });

    LvglLock lock;

    BatteryListScreen::Callbacks list_cb;
    list_cb.back = [this] { show(Page::Overview); };
    list_cb.settings = [this] { show(Page::Select); };
    list_cb.open = [this](uint8_t slot) {
        detail_slot_ = slot;
        show(Page::Detail);
    };
    list_.create(std::move(list_cb));

    BatteryDetailScreen::Callbacks detail_cb;
    detail_cb.back = [this] { show(Page::List); };
    detail_cb.page = [this](int step) { page_detail(step); };
    detail_.create(std::move(detail_cb));

    BmsSelectScreen::Callbacks select_cb;
    select_cb.back = [this] { show(Page::List); };
    select_cb.scan = [this] {
        if (cb_.start_discovery) cb_.start_discovery();
    };
    select_cb.save = [this](const std::vector<BmsDeviceId> &selection) {
        if (cb_.save_selection) cb_.save_selection(selection);
        show(Page::List);
    };
    select_.create(std::move(select_cb));
}

void App::update(const BatteryData *batteries, uint8_t count, const std::vector<FoundBms> &found,
                 bool scanning)
{
    LvglLock lock;
    count_ = std::min<uint8_t>(count, batteries_.size());
    std::copy(batteries, batteries + count_, batteries_.begin());
    found_ = found;
    scanning_ = scanning;
    refresh();
}

void App::show(Page page)
{
    LvglLock lock;  // recursive: also fine when called from an LVGL event
    page_ = page;
    switch (page) {
        case Page::Overview:
            lv_screen_load(overview_.screen());
            break;
        case Page::List:
            lv_screen_load(list_.screen());
            break;
        case Page::Detail:
            lv_screen_load(detail_.screen());
            break;
        case Page::Select:
            select_.open(cb_.monitored ? cb_.monitored() : std::vector<BmsDeviceId>());
            if (cb_.start_discovery) cb_.start_discovery();
            lv_screen_load(select_.screen());
            break;
    }
    refresh();
}

void App::refresh()
{
    switch (page_) {
        case Page::Overview:
            break;  // updated directly through overview()
        case Page::List:
            list_.update(batteries_.data(), count_);
            break;
        case Page::Detail:
            /* The battery was deselected meanwhile: back to the list */
            if (detail_slot_ >= count_ || batteries_[detail_slot_].bmsName.empty()) {
                show(Page::List);
                return;
            }
            detail_.update(batteries_.data(), count_, detail_slot_);
            break;
        case Page::Select:
            select_.update(found_, scanning_, batteries_.data(), count_);
            break;
    }
}

void App::page_detail(int step)
{
    const BatterySlots used = used_slots(batteries_.data(), count_);
    for (int i = 0; i < used.count; i++) {
        if (used.index[i] != detail_slot_) continue;
        const int next = i + step;
        if (next >= 0 && next < used.count) {
            detail_slot_ = used.index[next];
            refresh();
        }
        return;
    }
}

}  // namespace ui
