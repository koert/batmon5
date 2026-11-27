/**
 * JKBmsBle - JK BMS Bluetooth Low Energy client
 *
 * Connects to up to JKBmsBle::kMaxBatteries JK BMS units over BLE and writes their
 * data into the BatteryData array passed to begin().
 *
 * Ported from batmon4 (Arduino BLE library) to ESP-IDF with esp-nimble-cpp.
 *
 * Threading:
 *  - loop() runs in its own FreeRTOS task (startTask()); blocking BLE calls are fine there.
 *  - The NimBLE host task runs the scan, disconnect and notify callbacks. They only set
 *    flags or queue bytes (small stack, must not block).
 *  - The UI reads data only through copyBatteryData().
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#include "NimBLEDevice.h"

#include "jk_bms/battery_data.hpp"

// Per-BMS connection state
struct BmsConnection {
    NimBLEClient* pClient = nullptr;          // created once per slot and reused, never deleted (see connectToDevice)
    StreamBufferHandle_t rxStream = nullptr;  // notification bytes: NimBLE host task -> bms task
    NimBLERemoteService* pService = nullptr;
    NimBLERemoteCharacteristic* pChar = nullptr;
    NimBLEAddress address;
    std::string name;
    volatile bool connected = false;  // written by the NimBLE host task
    bool deviceInfoReceived = false;
    bool cellInfoRequested = false;   // CELL_INFO requested after DEVICE_INFO reply
    volatile bool connectPending = false;  // found/re-found by scan: connect as soon as scanning stops
    uint32_t lastDataRequest = 0;
    volatile uint32_t lastDataReceived = 0;  // written by the NimBLE host task
    uint32_t lastReconnectAttempt = 0;
    uint32_t lastCommandSent = 0;
    uint8_t rxBuffer[512];
    size_t rxBufferLen = 0;
    uint8_t batteryIndex = 0;         // battery slot == display position
    uint8_t cachedSoc = 0;            // last good SOC, used when a frame has none
    float cachedCurrent = 0.0f;       // last good current, used when a frame has none
    enum class State {
        IDLE,
        CONNECTING,
        DISCOVERING_SERVICES,
        ENABLING_NOTIFICATIONS,
        CONNECTED,
        DISCONNECTED,
        ERROR
    };
    volatile State connectionState = State::IDLE;
};

class JKBmsBle {
public:
    static constexpr uint8_t kMaxBatteries = 2;

    JKBmsBle();

    // Initialize BLE and start scanning for BMS devices named CONFIG_JK_BMS_NAME_PREFIX...
    bool begin(BatteryData* batteries, uint8_t maxBatteries);

    // Run loop() in its own FreeRTOS task so blocking BLE calls never stall the UI.
    // Call after begin().
    bool startTask(BaseType_t core = 0);

    // Connect/discover/notify state machine, rescans; run by the task from startTask()
    void loop();

    // Thread-safe copy of the battery data (use this from the UI instead of
    // reading the array passed to begin() directly)
    void copyBatteryData(BatteryData* dest, uint8_t count);

    // Enable/disable monitoring (safe to call from any task; the BMS task applies it).
    // Disabled: scanning stops, all BMS connections are closed, battery values are cleared
    // and incoming data is ignored. Enabled again: scan/connect resumes.
    // Call before begin() to start with monitoring off.
    void setMonitoringEnabled(bool enabled) { _monitoringEnabled = enabled; }

private:
    class ScanCallbacks : public NimBLEScanCallbacks {
    public:
        explicit ScanCallbacks(JKBmsBle* parent) : _parent(parent) {}
        void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override;
        void onScanEnd(const NimBLEScanResults& results, int reason) override;
    private:
        JKBmsBle* _parent;
    };

    class ClientCallbacks : public NimBLEClientCallbacks {
    public:
        ClientCallbacks(JKBmsBle* parent, uint8_t connIndex) : _parent(parent), _connIndex(connIndex) {}
        void onConnect(NimBLEClient* pClient) override;
        void onDisconnect(NimBLEClient* pClient, int reason) override;
    private:
        JKBmsBle* _parent;
        uint8_t _connIndex;
    };

    static void taskEntry(void* arg);

    // Connection handling (bms task)
    bool connectToDevice(uint8_t connIndex);
    bool discoverServices(uint8_t connIndex);
    bool enableNotifications(uint8_t connIndex);
    bool requestData(uint8_t connIndex);
    void disconnect(uint8_t connIndex);
    void checkHealth(uint8_t connIndex, uint32_t now);
    uint8_t getConnectedCount() const;

    // Frame handling (bms task)
    void processReceivedData(uint8_t connIndex);  // drain rxStream into handleNotification()
    void handleNotification(uint8_t connIndex, const uint8_t* data, size_t length);
    bool parseResponse(uint8_t connIndex, const uint8_t* frame, BatteryData& out);
    void mergeBatteryData(BmsConnection& conn, const BatteryData& parsed);
    void expireStaleData();  // clear values of batteries silent for kDataExpiryMs

    // Scanning
    int findConnectionByAddress(const NimBLEAddress& address);
    int findFreeConnectionSlot();
    void startScan();
    void stopScanIfAllSlotsKnown();

    // Monitoring on/off (bms task)
    void stopMonitoring();    // stop scan, disconnect all, clear values
    void resumeMonitoring();  // start connecting/scanning again

    NimBLEScan* _pScan = nullptr;
    ScanCallbacks _scanCallbacks{this};
    ClientCallbacks* _clientCallbacks[kMaxBatteries] = {};

    // One connection per battery slot; slots [0, _numConnections) have a known device
    BmsConnection _connections[kMaxBatteries];
    volatile uint8_t _numConnections = 0;

    // Battery data, guarded by _dataMutex (written from the BMS task, read by the UI)
    BatteryData* _batteries = nullptr;
    uint8_t _maxBatteries = 0;
    SemaphoreHandle_t _dataMutex = nullptr;
    TaskHandle_t _task = nullptr;

    uint32_t _lastScanTime = 0;
    volatile bool _scanning = false;          // cleared by the scan-end callback (NimBLE host task)
    volatile bool _monitoringEnabled = true;  // requested state, set by the UI
    bool _monitoringStopped = false;          // applied state, only touched by the BMS task
};
