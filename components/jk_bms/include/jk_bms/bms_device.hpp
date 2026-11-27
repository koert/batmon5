#pragma once

#include <cstdint>
#include <string>

/** Identifies one JK BMS: its BLE address (the identity) and its advertised name (for display). */
struct BmsDeviceId {
    std::string address;      // "c8:47:8c:12:a4:01" (NimBLEAddress::toString())
    uint8_t addressType = 0;  // BLE address type (0 = public)
    std::string name;         // advertised name, e.g. "Accu 1"
};

/** A JK BMS seen by the BLE scan. */
struct FoundBms {
    BmsDeviceId id;
    int8_t rssi = 0;     // dBm of the last advertisement
    uint32_t ageMs = 0;  // time since the last advertisement
};
