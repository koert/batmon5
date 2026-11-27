#pragma once

#include <optional>
#include <vector>

#include "esp_err.h"
#include "jk_bms/bms_device.hpp"

/**
 * The BMS selection in NVS (namespace "bms"). nvs_flash_init() must have run.
 */
namespace bms_settings {

/** The saved selection, or an empty optional when none was ever saved. */
std::optional<std::vector<BmsDeviceId>> load();

/** Save the selection (an empty list is a valid selection: monitor nothing). */
esp_err_t save(const std::vector<BmsDeviceId> &devices);

}  // namespace bms_settings
