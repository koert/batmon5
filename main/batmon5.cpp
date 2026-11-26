#include "display.hpp"
#include "ui_overview.hpp"

#include "esp_err.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "batmon5";
ui::OverviewScreen overview;
}  // namespace

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(display::init());
    overview.create();

    /* Sample values from docs/ui/overview.svg until real sensors are connected. */
    overview.set_battery(78, 124.0f, 12.84f);
    overview.set_solar(186.0f, "Bulk");
    overview.set_water(65, 40);

    ESP_LOGI(TAG, "started");
}
