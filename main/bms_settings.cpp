#include "bms_settings.hpp"

#include <array>
#include <cstdio>

#include "esp_log.h"
#include "nvs.h"

namespace bms_settings {
namespace {

constexpr const char *TAG = "bms_settings";
constexpr const char *kNamespace = "bms";
constexpr uint8_t kMaxDevices = 8;

/* Keys: "count", and per device i: "addr<i>", "type<i>", "name<i>". */
using Key = std::array<char, 16>;

Key key(const char *base, unsigned i)
{
    Key k{};
    std::snprintf(k.data(), k.size(), "%s%u", base, i);
    return k;
}

bool read_str(nvs_handle_t nvs, const char *k, std::string &out)
{
    size_t len = 0;
    if (nvs_get_str(nvs, k, nullptr, &len) != ESP_OK || len == 0) {
        return false;
    }
    std::string value(len, '\0');
    if (nvs_get_str(nvs, k, value.data(), &len) != ESP_OK) {
        return false;
    }
    value.resize(len - 1);  // drop the terminating NUL
    out = value;
    return true;
}

}  // namespace

std::optional<std::vector<BmsDeviceId>> load()
{
    nvs_handle_t nvs;
    if (nvs_open(kNamespace, NVS_READONLY, &nvs) != ESP_OK) {
        return std::nullopt;  // namespace doesn't exist yet: never saved
    }

    uint8_t count = 0;
    if (nvs_get_u8(nvs, "count", &count) != ESP_OK) {
        nvs_close(nvs);
        return std::nullopt;
    }

    std::vector<BmsDeviceId> devices;
    for (unsigned i = 0; i < count && i < kMaxDevices; i++) {
        BmsDeviceId dev;
        if (!read_str(nvs, key("addr", i).data(), dev.address)) {
            ESP_LOGW(TAG, "device %u has no address, skipped", i);
            continue;
        }
        nvs_get_u8(nvs, key("type", i).data(), &dev.addressType);
        read_str(nvs, key("name", i).data(), dev.name);
        devices.push_back(dev);
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "loaded %u BMS device(s)", static_cast<unsigned>(devices.size()));
    return devices;
}

esp_err_t save(const std::vector<BmsDeviceId> &devices)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_erase_all(nvs);  // drop the keys of a longer, older list
    const uint8_t count = devices.size() < kMaxDevices ? devices.size() : kMaxDevices;
    err = nvs_set_u8(nvs, "count", count);
    for (unsigned i = 0; i < count && err == ESP_OK; i++) {
        err = nvs_set_str(nvs, key("addr", i).data(), devices[i].address.c_str());
        if (err == ESP_OK) err = nvs_set_u8(nvs, key("type", i).data(), devices[i].addressType);
        if (err == ESP_OK) err = nvs_set_str(nvs, key("name", i).data(), devices[i].name.c_str());
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved %u BMS device(s)", count);
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

}  // namespace bms_settings
