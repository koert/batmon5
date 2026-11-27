#include "display.hpp"
#include "ui_overview.hpp"

#include <optional>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "jk_bms/jk_bms_ble.hpp"

namespace {

constexpr const char *TAG = "batmon5";
constexpr uint8_t kNumBatteries = JKBmsBle::kMaxBatteries;
constexpr TickType_t kUiUpdateInterval = pdMS_TO_TICKS(1000);

ui::OverviewScreen overview;
JKBmsBle jk_bms;

// Written by the BMS task under its mutex; the UI only reads copies (copyBatteryData()).
BatteryData batteries[kNumBatteries];
BatteryData ui_batteries[kNumBatteries];

// Show the batteries as one bank: average voltage and SOC, total power.
// Only batteries with current data count; none at all shows "--".
void update_battery_view()
{
    jk_bms.copyBatteryData(ui_batteries, kNumBatteries);

    float voltage = 0.0f;
    float soc = 0.0f;
    float power = 0.0f;
    int count = 0;
    for (const BatteryData &bat : ui_batteries) {
        if (bat.hasData()) {
            voltage += bat.voltage;
            soc += bat.soc;
            power += bat.power;
            count++;
        }
    }

    if (count == 0) {
        overview.set_battery(std::nullopt, std::nullopt, std::nullopt);
    } else {
        overview.set_battery(static_cast<int>(soc / count + 0.5f), power, voltage / count);
    }
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(display::init());
    overview.create();

    /* Sample values until the solar charger and tank sensors are connected. */
    overview.set_solar(186.0f, "Bulk");
    overview.set_water(65, 40);

    if (jk_bms.begin(batteries, kNumBatteries)) {
        jk_bms.startTask(0);  // BLE work on core 0, next to the NimBLE host
    } else {
        ESP_LOGE(TAG, "JK BMS client failed to start");
    }

    ESP_LOGI(TAG, "started");

    for (;;) {
        update_battery_view();
        vTaskDelay(kUiUpdateInterval);
    }
}
