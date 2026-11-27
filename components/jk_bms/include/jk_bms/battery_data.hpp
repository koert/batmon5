#pragma once

#include <cstdint>
#include <string>

/**
 * Live values of one battery, as read from its JK BMS.
 * Shared between the BMS client and the UI.
 */
struct BatteryData {
    static constexpr int kMaxCells = 32;  // JK BMS supports up to 32 cells (JK02_32S frames)


    float voltage = -1.0f;
    float current = -1.0f;        // A, positive = charging
    float power = -1.0f;          // W, positive = charging
    float soc = -1.0f;            // %
    float temp1 = 0.0f;
    float temp2 = 0.0f;
    uint8_t cellCount = 0;                // number of cells (series), from the BMS; 0 = unknown
    float cellVoltages[kMaxCells] = {0};  // V, the first cellCount are valid
    float cellDiff = 0.0f;                // max cell voltage difference
    uint32_t timestamp = 0;       // ms since boot of the last update, 0 = no data
    std::string bmsName;          // JK BMS device name (e.g. "Accu 1")
    bool connected = false;       // BLE link up and streaming (set by copyBatteryData)

    /** True when this battery has current values (they expire some time after the last frame). */
    bool hasData() const { return timestamp > 0 && voltage > 0 && soc >= 0; }
};
