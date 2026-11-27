#include <optional>
#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "bms_settings.hpp"
#include "display.hpp"
#include "jk_bms/jk_bms_ble.hpp"
#include "ui_app.hpp"

namespace {

constexpr const char *TAG = "batmon5";
constexpr uint8_t kNumBatteries = JKBmsBle::kMaxBatteries;
constexpr TickType_t kUiUpdateInterval = pdMS_TO_TICKS(500);

ui::App app;
JKBmsBle jk_bms;
BatteryData batteries[kNumBatteries];     // written by the BMS client
BatteryData ui_batteries[kNumBatteries];  // copy for the UI

void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated or changed format: erasing it");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* Overview battery card: the average SOC and voltage and the total power of all batteries. */
void update_overview()
{
    float voltage = 0, soc = 0, power = 0;
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
        app.overview().set_battery(std::nullopt, std::nullopt, std::nullopt);
    } else {
        app.overview().set_battery(static_cast<int>(soc / count + 0.5f), power, voltage / count);
    }
}

}  // namespace

extern "C" void app_main(void)
{
    init_nvs();  // BMS selection, and the RF calibration data of the BLE radio
    ESP_ERROR_CHECK(display::init());

    ui::App::Callbacks callbacks;
    callbacks.monitored = [] { return jk_bms.getMonitoredDevices(); };
    callbacks.start_discovery = [] { jk_bms.requestDiscovery(); };
    callbacks.save_selection = [](const std::vector<BmsDeviceId> &selection) {
        bms_settings::save(selection);
        jk_bms.setSelection(selection);
    };
    app.create(std::move(callbacks));
    app.overview().set_solar(186.0f, "Bulk");
    app.overview().set_water(65, 40);

    const std::optional<std::vector<BmsDeviceId>> selection = bms_settings::load();
    if (jk_bms.begin(batteries, kNumBatteries, selection ? &*selection : nullptr)) {
        jk_bms.startTask(0);
    } else {
        ESP_LOGE(TAG, "JK BMS client failed to start");
    }
    ESP_LOGI(TAG, "started");

    for (;;) {
        jk_bms.copyBatteryData(ui_batteries, kNumBatteries);
        update_overview();
        app.update(ui_batteries, kNumBatteries, jk_bms.getFoundDevices(), jk_bms.isScanning());
        vTaskDelay(kUiUpdateInterval);
    }
}
