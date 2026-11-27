/**
 * JKBmsBle - JK BMS Bluetooth Low Energy client implementation
 *
 * Protocol follows syssi/esphome-jk-bms (jk_bms_ble):
 *   request:  AA 55 90 EB cmd len value(4) padding(9) checksum   (20 bytes)
 *   response: 55 AA EB 90 payload(295) checksum                  (300 bytes, split over several
 *             notifications)
 * checksum = sum of all preceding bytes, modulo 256.
 *
 * Ported from batmon4 (src/bms/JKBmsBle.cpp, Arduino BLE library) to ESP-IDF with
 * esp-nimble-cpp. The protocol, state machine and parser are unchanged; Arduino calls were
 * replaced: millis() -> now_ms(), delay() -> sleep_ms(), Serial -> ESP_LOG, String -> std::string.
 */

#include "jk_bms/jk_bms_ble.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

namespace {

constexpr const char* TAG = "jk_bms";

// JK BMS BLE service and characteristic
const NimBLEUUID kServiceUuid("0000ffe0-0000-1000-8000-00805f9b34fb");
const NimBLEUUID kCharUuid("0000ffe1-0000-1000-8000-00805f9b34fb");  // notify + write

// Timing (ms unless noted)
constexpr uint32_t kReconnectInterval   = 30000;
constexpr uint32_t kHealthCheckTimeout  = 30000;   // disconnect after this long without data...
constexpr uint32_t kCommandGraceMs      = 20000;   // ...unless a command was sent within this time
constexpr uint32_t kScanDurationMs      = 10000;   // short non-blocking scans...
constexpr uint32_t kRescanInterval      = 30000;   // ...repeated only while a battery is missing
constexpr uint32_t kDataRequestInterval = 15000;
constexpr uint32_t kDataExpiryMs        = 300000;  // clear a battery's values 5 min after its last data
constexpr uint32_t kConnectTimeoutMs    = 5000;

// Bytes of notification data buffered between the NimBLE host task and the bms task
constexpr size_t kRxStreamSize = 2048;

constexpr uint8_t REQUEST_HEADER[4]  = {0xAA, 0x55, 0x90, 0xEB};
constexpr uint8_t RESPONSE_HEADER[4] = {0x55, 0xAA, 0xEB, 0x90};

constexpr uint8_t JK_BMS_CMD_CELL_INFO   = 0x96;
constexpr uint8_t JK_BMS_CMD_DEVICE_INFO = 0x97;
constexpr size_t JK_BMS_REQUEST_SIZE     = 20;
constexpr size_t JK_BMS_RESPONSE_SIZE    = 300;
constexpr size_t JK_BMS_PAYLOAD_SIZE     = JK_BMS_RESPONSE_SIZE - 5;  // without header and checksum

// Response frame type, byte 4 of the frame (payload[0]); byte 5 is a frame counter
constexpr uint8_t JK_BMS_FRAME_SETTINGS    = 0x01;
constexpr uint8_t JK_BMS_FRAME_CELL_INFO   = 0x02;
constexpr uint8_t JK_BMS_FRAME_DEVICE_INFO = 0x03;

// Temperatures outside this range are treated as "not found"
constexpr float TEMP_VALID_MIN = -40.0f;
constexpr float TEMP_VALID_MAX = 85.0f;

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void sleep_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint8_t checksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return sum;
}

uint16_t u16At(const uint8_t* p, size_t off) {
    return (uint16_t) p[off] | ((uint16_t) p[off + 1] << 8);
}

uint32_t u32At(const uint8_t* p, size_t off) {
    return (uint32_t) p[off] | ((uint32_t) p[off + 1] << 8) |
           ((uint32_t) p[off + 2] << 16) | ((uint32_t) p[off + 3] << 24);
}

bool validTemp(float t) {
    return t >= TEMP_VALID_MIN && t <= TEMP_VALID_MAX;  // false for NAN
}

bool startsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

// Reset a battery's values but keep its name (the UI uses it as title)
void clearBatteryValues(BatteryData& bat) {
    std::string name = bat.bmsName;
    bat = BatteryData();
    bat.bmsName = name;
}

// Search backwards from the end of the payload for up to two temperatures in [minT, maxT].
// Continues an earlier search: `found` temperatures are already in temps[].
void searchTemps(const uint8_t* p, size_t len, bool int16, float minT, float maxT,
                 float temps[2], uint8_t& found) {
    for (int off = (int) len - (int16 ? 2 : 1); off >= 22 && found < 2; off--) {
        float t = int16 ? (int16_t) u16At(p, off) * 0.1f   // int16, 0.1 °C
                        : (int8_t) p[off] - 40.0f;         // int8, offset -40 °C
        if (t >= minT && t <= maxT) temps[found++] = t;
    }
}

}  // namespace

// ==============================================================================
// Setup and task
// ==============================================================================

JKBmsBle::JKBmsBle() {
    for (uint8_t i = 0; i < kMaxBatteries; i++) {
        _connections[i].batteryIndex = i;
    }
}

bool JKBmsBle::begin(BatteryData* batteries, uint8_t maxBatteries,
                     const std::vector<BmsDeviceId>* selection) {
    _batteries = batteries;
    _maxBatteries = std::min(maxBatteries, kMaxBatteries);
    if (!_dataMutex) {
        _dataMutex = xSemaphoreCreateMutex();
    }
    if (!_foundMutex) {
        _foundMutex = xSemaphoreCreateMutex();
        _found.reserve(kMaxFound);
    }
    for (uint8_t i = 0; i < kMaxBatteries; i++) {
        if (!_connections[i].rxStream) {
            _connections[i].rxStream = xStreamBufferCreate(kRxStreamSize, 1);
        }
    }

    if (!NimBLEDevice::init(CONFIG_JK_BMS_BLE_DEVICE_NAME)) {
        ESP_LOGE(TAG, "Failed to initialize BLE");
        return false;
    }
    _pScan = NimBLEDevice::getScan();
    if (!_pScan) {
        ESP_LOGE(TAG, "Failed to get BLE scan object");
        return false;
    }
    _pScan->setScanCallbacks(&_scanCallbacks, false);
    _pScan->setActiveScan(true);
    _pScan->setInterval(100);
    _pScan->setWindow(99);

    if (selection) {
        applySelection(*selection);  // starts the scan when it selects anything
    } else {
        ESP_LOGI(TAG, "No BMS selection saved: monitoring devices named '%s...'",
                 CONFIG_JK_BMS_NAME_PREFIX);
        if (_monitoringEnabled) {
            startScan();
        }
    }
    return true;
}

void JKBmsBle::setSelection(const std::vector<BmsDeviceId>& devices) {
    if (!_dataMutex) return;
    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    _pendingSelection = devices;
    if (_pendingSelection.size() > _maxBatteries) _pendingSelection.resize(_maxBatteries);
    _monitored = _pendingSelection;  // report the new selection right away
    _selectionPending = true;
    xSemaphoreGive(_dataMutex);
}

std::vector<BmsDeviceId> JKBmsBle::getMonitoredDevices() {
    std::vector<BmsDeviceId> result;
    if (!_dataMutex) return result;
    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    result = _monitored;
    xSemaphoreGive(_dataMutex);
    return result;
}

std::vector<FoundBms> JKBmsBle::getFoundDevices() {
    std::vector<FoundBms> result;
    if (!_foundMutex) return result;
    xSemaphoreTake(_foundMutex, portMAX_DELAY);
    uint32_t now = now_ms();
    result.reserve(_found.size());
    for (const FoundEntry& entry : _found) {
        FoundBms found;
        found.id = entry.id;
        found.rssi = entry.rssi;
        found.ageMs = now - entry.lastSeen;
        result.push_back(found);
    }
    xSemaphoreGive(_foundMutex);
    return result;
}

// bms task (or begin(), before the task runs)
void JKBmsBle::applySelection(const std::vector<BmsDeviceId>& devices) {
    if (_scanning) {
        _pScan->stop();
        _scanning = false;
    }
    _discoveryScan = false;
    for (uint8_t i = 0; i < _numConnections; i++) {
        disconnect(i);
        _connections[i].connectPending = false;
    }
    _numConnections = 0;

    uint8_t count = std::min<size_t>(devices.size(), _maxBatteries);
    uint32_t now = now_ms();
    for (uint8_t i = 0; i < count; i++) {
        BmsConnection& conn = _connections[i];
        conn.address = NimBLEAddress(devices[i].address, devices[i].addressType);
        conn.name = devices[i].name;
        conn.connectionState = BmsConnection::State::DISCONNECTED;
        conn.connectPending = false;   // connect once the scan has seen it...
        conn.lastReconnectAttempt = now;  // ...or at the next periodic retry
        ESP_LOGI(TAG, "[BMS%d] Selected '%s' %s", i, conn.name.c_str(), devices[i].address.c_str());
    }
    _selectionMode = true;

    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    _monitored.assign(devices.begin(), devices.begin() + count);
    if (_batteries) {
        for (uint8_t i = 0; i < _maxBatteries; i++) {
            _batteries[i] = BatteryData();
            if (i < count) _batteries[i].bmsName = devices[i].name;
        }
    }
    xSemaphoreGive(_dataMutex);

    _numConnections = count;
    if (count > 0 && _monitoringEnabled) {
        startScan();
    }
}

bool JKBmsBle::startTask(BaseType_t core) {
    if (_task) return true;
    // The NimBLE host runs on core 0; keep BLE work there and leave core 1 to the UI
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "bms", 8192, this, 1, &_task, core);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to start BMS task");
        _task = nullptr;
        return false;
    }
    return true;
}

void JKBmsBle::taskEntry(void* arg) {
    JKBmsBle* self = static_cast<JKBmsBle*>(arg);
    for (;;) {
        self->loop();
        sleep_ms(20);
    }
}

void JKBmsBle::copyBatteryData(BatteryData* dest, uint8_t count) {
    if (!_batteries || !_dataMutex) return;
    if (count > _maxBatteries) count = _maxBatteries;
    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    for (uint8_t i = 0; i < count; i++) {
        dest[i] = _batteries[i];
    }
    xSemaphoreGive(_dataMutex);

    // Connection state lives in BmsConnection (slot index == battery index), not under the mutex
    for (uint8_t i = 0; i < count; i++) {
        dest[i].connected = i < _numConnections && _connections[i].connected
                            && _connections[i].connectionState == BmsConnection::State::CONNECTED;
    }
}

// ==============================================================================
// Main loop (bms task)
// ==============================================================================

void JKBmsBle::loop() {
    // Apply monitoring on/off requests from the UI
    if (!_monitoringEnabled) {
        if (!_monitoringStopped) {
            stopMonitoring();
            _monitoringStopped = true;
        }
        return;
    }
    if (_monitoringStopped) {
        _monitoringStopped = false;
        resumeMonitoring();
    }

    // New selection from the UI
    if (_selectionPending) {
        xSemaphoreTake(_dataMutex, portMAX_DELAY);
        std::vector<BmsDeviceId> selection = _pendingSelection;
        _selectionPending = false;
        xSemaphoreGive(_dataMutex);
        applySelection(selection);
    }

    // Discovery scan requested by the UI: restart any running scan as a full-length one
    if (_discoveryRequested) {
        if (_scanning) {
            _pScan->stop();
            _scanning = false;
        }
        _discoveryScan = true;
        _discoveryRequested = false;
        startScan();
        if (!_scanning) _discoveryScan = false;
    }

    uint32_t now = now_ms();

    for (uint8_t i = 0; i < _numConnections; i++) {
        BmsConnection& conn = _connections[i];

        // The peer dropped the link (onDisconnect only sets flags): clean up here on the bms task
        if (!conn.connected && conn.pService) {
            disconnect(i);
        }

        // Parse notification data queued by the NimBLE host task
        processReceivedData(i);

        if (!conn.connected) {
            // Connect right after the scan found it, otherwise retry periodically.
            // Never connect while scanning; scans are short and stop early once all slots are known.
            bool due = conn.connectPending || (now - conn.lastReconnectAttempt > kReconnectInterval);
            if (due && !_scanning) {
                conn.connectPending = false;
                conn.lastReconnectAttempt = now;
                connectToDevice(i);
            }
            continue;
        }

        switch (conn.connectionState) {
            case BmsConnection::State::DISCOVERING_SERVICES:
                if (discoverServices(i)) {
                    conn.connectionState = BmsConnection::State::ENABLING_NOTIFICATIONS;
                } else {
                    disconnect(i);
                }
                break;

            case BmsConnection::State::ENABLING_NOTIFICATIONS:
                if (enableNotifications(i)) {
                    conn.connectionState = BmsConnection::State::CONNECTED;
                    if (_batteries && conn.batteryIndex < _maxBatteries) {
                        xSemaphoreTake(_dataMutex, portMAX_DELAY);
                        _batteries[conn.batteryIndex].bmsName = conn.name;
                        xSemaphoreGive(_dataMutex);
                    }
                    conn.lastDataReceived = now_ms();
                    conn.lastDataRequest = now_ms();
                    requestData(i);  // DEVICE_INFO
                } else {
                    disconnect(i);
                }
                break;

            case BmsConnection::State::CONNECTED: {
                // Request CELL_INFO as soon as the DEVICE_INFO reply arrived; after that the
                // BMS streams cell frames, re-request periodically in case it stops.
                // Only send a periodic command if we're not waiting for a response.
                bool needCellInfo = conn.deviceInfoReceived && !conn.cellInfoRequested;
                bool waitingForResponse = conn.lastCommandSent > 0 && conn.lastDataReceived <= conn.lastCommandSent;
                if (needCellInfo || (!waitingForResponse && now - conn.lastDataRequest > kDataRequestInterval)) {
                    if (requestData(i) && conn.deviceInfoReceived) {
                        conn.cellInfoRequested = true;
                    }
                    conn.lastDataRequest = now;
                }
                break;
            }

            default:
                break;
        }

        checkHealth(i, now);
    }

    // Rescan while a battery is missing (slot unassigned or disconnected) to pick up
    // new or re-addressed devices; no scanning once all batteries are connected
    if (!_scanning && getConnectedCount() < targetCount()) {
        bool anyPending = false;
        for (uint8_t i = 0; i < _numConnections; i++) {
            if (_connections[i].connectPending) anyPending = true;
        }
        if (!anyPending && now - _lastScanTime > kRescanInterval) {
            startScan();
        }
    }

    expireStaleData();
}

void JKBmsBle::checkHealth(uint8_t connIndex, uint32_t now) {
    BmsConnection& conn = _connections[connIndex];
    if (!conn.connected) return;

    // Signed differences: both timestamps can be a bit newer than `now` (set later in this
    // loop pass, or by the NimBLE host task)
    bool dataRecent = conn.lastDataReceived > 0
                      && (int32_t) (now - conn.lastDataReceived) <= (int32_t) kHealthCheckTimeout;
    bool commandRecent = conn.lastCommandSent > 0
                         && (int32_t) (now - conn.lastCommandSent) <= (int32_t) kCommandGraceMs;
    if (!dataRecent && !commandRecent) {
        ESP_LOGW(TAG, "[BMS%d] No data %lus, disconnecting", conn.batteryIndex,
                 (unsigned long) (kHealthCheckTimeout / 1000));
        disconnect(connIndex);
        return;
    }

    if (conn.pClient && !conn.pClient->isConnected()) {
        ESP_LOGW(TAG, "[BMS%d] BLE client disconnected", conn.batteryIndex);
        disconnect(connIndex);
    }
}

uint8_t JKBmsBle::targetCount() const {
    // Selection: every selected device; name prefix: as many as there are battery slots
    return _selectionMode ? _numConnections : _maxBatteries;
}

uint8_t JKBmsBle::getConnectedCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < _numConnections; i++) {
        if (_connections[i].connected) count++;
    }
    return count;
}

void JKBmsBle::expireStaleData() {
    if (!_batteries || !_dataMutex) return;
    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    // Read the time under the lock: timestamps are written from the BLE task, so a value
    // captured earlier could be older than a timestamp and underflow the subtraction
    uint32_t now = now_ms();
    for (uint8_t i = 0; i < _maxBatteries; i++) {
        BatteryData& bat = _batteries[i];
        if (bat.timestamp > 0 && now - bat.timestamp > kDataExpiryMs) {
            clearBatteryValues(bat);
        }
    }
    xSemaphoreGive(_dataMutex);
}

// ==============================================================================
// Monitoring on/off (bms task)
// ==============================================================================

void JKBmsBle::stopMonitoring() {
    ESP_LOGI(TAG, "Monitoring off: stopping scan and disconnecting");
    if (_scanning) {
        _pScan->stop();
        _scanning = false;
    }
    for (uint8_t i = 0; i < _numConnections; i++) {
        disconnect(i);
        _connections[i].connectPending = false;
    }
    if (_batteries && _dataMutex) {
        xSemaphoreTake(_dataMutex, portMAX_DELAY);
        for (uint8_t i = 0; i < _maxBatteries; i++) {
            clearBatteryValues(_batteries[i]);
        }
        xSemaphoreGive(_dataMutex);
    }
}

void JKBmsBle::resumeMonitoring() {
    ESP_LOGI(TAG, "Monitoring on: reconnecting");
    // Known batteries connect right away; scan if a slot is still unassigned
    for (uint8_t i = 0; i < _numConnections; i++) {
        _connections[i].connectPending = true;
    }
    if (_numConnections < targetCount()) {
        startScan();
    }
}

// ==============================================================================
// Scanning
// ==============================================================================

void JKBmsBle::startScan() {
    _scanning = true;
    _lastScanTime = now_ms();
    // Non-blocking: start() returns immediately, onScanEnd() runs when the scan ends
    if (!_pScan->start(kScanDurationMs, false, true)) {
        _scanning = false;
    }
}

void JKBmsBle::ScanCallbacks::onScanEnd(const NimBLEScanResults& results, int reason) {
    _parent->_scanning = false;
    _parent->_discoveryScan = false;
}

// Stop the scan early once every battery that should be connected is known and either connected
// or waiting to connect (loop() only connects while not scanning). Discovery scans run to the end.
void JKBmsBle::stopScanIfAllSlotsKnown() {
    if (!_scanning || _discoveryScan) return;
    if (!_selectionMode && _numConnections < _maxBatteries) return;
    for (uint8_t i = 0; i < _numConnections; i++) {
        if (!_connections[i].connected && !_connections[i].connectPending) return;
    }
    _pScan->stop();
    _scanning = false;
}

// NimBLE host task: remember every JK BMS for the selection screen
void JKBmsBle::recordFound(const NimBLEAddress& address, const std::string& name, int8_t rssi) {
    if (!_foundMutex) return;
    std::string addr = address.toString();
    xSemaphoreTake(_foundMutex, portMAX_DELAY);
    FoundEntry* entry = nullptr;
    for (FoundEntry& e : _found) {
        if (e.id.address == addr) entry = &e;
    }
    if (!entry) {
        if (_found.size() >= kMaxFound) {
            // Full: replace the device that was seen longest ago
            entry = &*std::min_element(_found.begin(), _found.end(),
                [](const FoundEntry& a, const FoundEntry& b) { return a.lastSeen < b.lastSeen; });
        } else {
            _found.emplace_back();
            entry = &_found.back();
        }
        *entry = FoundEntry();
        entry->id.address = addr;
        entry->id.addressType = address.getType();
    }
    if (!name.empty()) entry->id.name = name;  // advertisements without scan response have no name
    entry->rssi = rssi;
    entry->lastSeen = now_ms();
    xSemaphoreGive(_foundMutex);
}

int JKBmsBle::findConnectionByAddress(const NimBLEAddress& address) {
    for (uint8_t i = 0; i < _numConnections; i++) {
        if (_connections[i].address == address) return i;
    }
    return -1;
}

int JKBmsBle::findFreeConnectionSlot() {
    return _numConnections < kMaxBatteries ? _numConnections : -1;
}

// NimBLE host task: only record the device, loop() connects
void JKBmsBle::ScanCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    if (!advertisedDevice->isAdvertisingService(kServiceUuid)) return;
    std::string name = advertisedDevice->getName();
    NimBLEAddress address = advertisedDevice->getAddress();
    _parent->recordFound(address, name, advertisedDevice->getRSSI());

    if (!_parent->_monitoringEnabled) return;  // scan result arriving while monitoring is being turned off

    // Known device: reconnect if disconnected
    int existingIdx = _parent->findConnectionByAddress(address);
    if (existingIdx >= 0) {
        ESP_LOGI(TAG, "[SCAN] BMS%d '%s' %s RSSI=%d", existingIdx, name.c_str(),
                 address.toString().c_str(), advertisedDevice->getRSSI());
        if (!_parent->_connections[existingIdx].connected) {
            _parent->_connections[existingIdx].connectPending = true;
        }
        _parent->stopScanIfAllSlotsKnown();
        return;
    }

    // With a selection, only the selected devices (matched by address above) are monitored
    if (_parent->_selectionMode) return;
    if (!startsWith(name, CONFIG_JK_BMS_NAME_PREFIX)) return;
    ESP_LOGI(TAG, "[SCAN] JK BMS: '%s' %s RSSI=%d", name.c_str(), address.toString().c_str(),
             advertisedDevice->getRSSI());

    // Known name with a new address (random MAC)
    for (uint8_t i = 0; i < _parent->_numConnections; i++) {
        BmsConnection& conn = _parent->_connections[i];
        if (conn.name == name && !conn.connected) {
            conn.address = address;
            conn.connectPending = true;
            xSemaphoreTake(_parent->_dataMutex, portMAX_DELAY);
            if (i < _parent->_monitored.size()) {
                _parent->_monitored[i].address = address.toString();
                _parent->_monitored[i].addressType = address.getType();
            }
            xSemaphoreGive(_parent->_dataMutex);
            _parent->stopScanIfAllSlotsKnown();
            return;
        }
    }

    // New device: take the next free slot
    int freeSlot = _parent->findFreeConnectionSlot();
    if (freeSlot < 0) return;

    BmsConnection& conn = _parent->_connections[freeSlot];
    conn.address = address;
    conn.name = name;
    conn.connectionState = BmsConnection::State::DISCONNECTED;
    conn.connectPending = true;

    BmsDeviceId id;
    id.address = address.toString();
    id.addressType = address.getType();
    id.name = name;
    xSemaphoreTake(_parent->_dataMutex, portMAX_DELAY);
    _parent->_monitored.push_back(id);
    xSemaphoreGive(_parent->_dataMutex);

    // Publish the slot only after it is fully set up (loop() runs on another task)
    _parent->_numConnections = freeSlot + 1;

    // Keep scanning until every battery slot is known, then connect from loop()
    _parent->stopScanIfAllSlotsKnown();
}

// ==============================================================================
// Connection handling
// ==============================================================================

void JKBmsBle::ClientCallbacks::onConnect(NimBLEClient* pClient) {
    BmsConnection& conn = _parent->_connections[_connIndex];
    conn.connected = true;
    conn.pClient = pClient;
    conn.deviceInfoReceived = false;
    conn.cellInfoRequested = false;
    conn.lastDataReceived = now_ms();
    conn.rxBufferLen = 0;
    conn.connectionState = BmsConnection::State::DISCOVERING_SERVICES;
    ESP_LOGI(TAG, "[BMS%d] Connected, MTU=%d", conn.batteryIndex, pClient->getMTU());
}

void JKBmsBle::ClientCallbacks::onDisconnect(NimBLEClient* pClient, int reason) {
    // Runs on the NimBLE host task while the bms task may be using the client: only set flags.
    // The client and its service/characteristic pointers stay valid; loop() cleans up via disconnect().
    BmsConnection& conn = _parent->_connections[_connIndex];
    conn.connected = false;
    conn.lastReconnectAttempt = now_ms();
    conn.connectionState = BmsConnection::State::DISCONNECTED;
    ESP_LOGI(TAG, "[BMS%d] Disconnected (reason %d)", conn.batteryIndex, reason);
}

bool JKBmsBle::connectToDevice(uint8_t connIndex) {
    BmsConnection& conn = _connections[connIndex];
    conn.connectionState = BmsConnection::State::CONNECTING;

    // One client per slot, reused for every attempt. NimBLE passes the client object to its GAP
    // event handler, and late events (e.g. a cancelled connect on a weak link) can still arrive
    // after a failed connect(); deleting the client then crashes the host task.
    if (!conn.pClient) {
        conn.pClient = NimBLEDevice::createClient();
        if (!conn.pClient) return false;
        _clientCallbacks[connIndex] = new ClientCallbacks(this, connIndex);
        conn.pClient->setClientCallbacks(_clientCallbacks[connIndex], false);
        // A device that is out of range would otherwise block this task for 30 s per attempt
        conn.pClient->setConnectTimeout(kConnectTimeoutMs);
    }

    const int maxRetries = 3;
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        _pScan->stop();
        sleep_ms(200);
        _scanning = false;

        if (conn.pClient->connect(conn.address)) {
            // Longer supervision timeout (6 s instead of 4 s) so a weak link survives missed packets
            conn.pClient->updateConnParams(24, 40, 0, 600);
            return true;
        }
        if (attempt < maxRetries) sleep_ms(1000);
    }
    conn.connectionState = BmsConnection::State::ERROR;
    // Keep the address: retries use it, and a rescan updates it if the MAC changed
    ESP_LOGW(TAG, "[BMS%d] Connection failed after %d attempts", conn.batteryIndex, maxRetries);
    return false;
}

bool JKBmsBle::discoverServices(uint8_t connIndex) {
    BmsConnection& conn = _connections[connIndex];
    if (!conn.pClient || !conn.pClient->isConnected()) return false;
    // The client is reused: rediscover instead of using the services cached from a previous connection
    conn.pClient->getServices(true);
    conn.pService = conn.pClient->getService(kServiceUuid);
    if (!conn.pService) return false;
    conn.pChar = conn.pService->getCharacteristic(kCharUuid);
    return conn.pChar != nullptr;
}

bool JKBmsBle::enableNotifications(uint8_t connIndex) {
    BmsConnection& conn = _connections[connIndex];
    if (!conn.pClient || !conn.pClient->isConnected()) return false;
    if (!conn.pChar || !conn.pChar->canNotify()) return false;

    StreamBufferHandle_t stream = conn.rxStream;
    if (!stream) return false;
    xStreamBufferReset(stream);
    // Runs on the NimBLE host task (small stack, must not block): only queue the bytes,
    // the bms task parses them in processReceivedData().
    // subscribe() writes 0x0001 (notifications) to the CCCD itself.
    bool ok = conn.pChar->subscribe(
        true,
        [this, connIndex, stream](NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
            if (!_monitoringEnabled) return;
            _connections[connIndex].lastDataReceived = now_ms();
            xStreamBufferSend(stream, data, length, 0);
        });
    sleep_ms(200);
    return ok;
}

// Sends DEVICE_INFO until its reply has arrived, CELL_INFO after that
bool JKBmsBle::requestData(uint8_t connIndex) {
    BmsConnection& conn = _connections[connIndex];
    if (!conn.pChar) return false;

    uint8_t frame[JK_BMS_REQUEST_SIZE] = {0};
    memcpy(frame, REQUEST_HEADER, sizeof(REQUEST_HEADER));
    frame[4] = conn.deviceInfoReceived ? JK_BMS_CMD_CELL_INFO : JK_BMS_CMD_DEVICE_INFO;
    // frame[5] (length) and [6..9] (value) stay 0, [10..18] are padding
    frame[JK_BMS_REQUEST_SIZE - 1] = checksum(frame, JK_BMS_REQUEST_SIZE - 1);

    // Try both without and with response
    for (bool withResponse : {false, true}) {
        if (conn.pChar->writeValue(frame, JK_BMS_REQUEST_SIZE, withResponse)) {
            conn.lastCommandSent = now_ms();
            sleep_ms(50);
            return true;
        }
    }
    return false;
}

void JKBmsBle::disconnect(uint8_t connIndex) {
    BmsConnection& conn = _connections[connIndex];
    if (conn.pClient && conn.pClient->isConnected()) {
        conn.pClient->disconnect();
        // Wait for the disconnect event so the client can be reused for the next connect()
        uint32_t start = now_ms();
        while (conn.pClient->isConnected() && now_ms() - start < 3000) {
            sleep_ms(20);
        }
    }
    if (conn.rxStream) xStreamBufferReset(conn.rxStream);
    conn.connected = false;
    conn.deviceInfoReceived = false;
    conn.cellInfoRequested = false;
    conn.cachedSoc = 0;
    conn.cachedCurrent = 0.0f;
    conn.pService = nullptr;
    conn.pChar = nullptr;
    conn.rxBufferLen = 0;
    conn.connectionState = BmsConnection::State::DISCONNECTED;
}

// ==============================================================================
// Frame handling (bms task)
// ==============================================================================

void JKBmsBle::processReceivedData(uint8_t connIndex) {
    StreamBufferHandle_t stream = _connections[connIndex].rxStream;
    if (!stream) return;
    uint8_t chunk[256];
    size_t n;
    while ((n = xStreamBufferReceive(stream, chunk, sizeof(chunk), 0)) > 0) {
        handleNotification(connIndex, chunk, n);
    }
}

// Collects notification bytes into rxBuffer and handles every complete response frame
void JKBmsBle::handleNotification(uint8_t connIndex, const uint8_t* data, size_t length) {
    if (!_monitoringEnabled) return;  // data queued before monitoring was turned off
    BmsConnection& conn = _connections[connIndex];

    if (conn.rxBufferLen + length <= sizeof(conn.rxBuffer)) {
        memcpy(conn.rxBuffer + conn.rxBufferLen, data, length);
        conn.rxBufferLen += length;
    } else {
        ESP_LOGW(TAG, "[BMS%d] [NOTIFY] Buffer overflow, resetting", conn.batteryIndex);
        conn.rxBufferLen = 0;
    }

    while (conn.rxBufferLen >= sizeof(RESPONSE_HEADER)) {
        // Drop everything before the response header (the last 3 bytes are kept when there is
        // none: they may be the start of a header)
        size_t start = 0;
        while (start + 3 < conn.rxBufferLen &&
               memcmp(conn.rxBuffer + start, RESPONSE_HEADER, sizeof(RESPONSE_HEADER)) != 0) {
            start++;
        }
        if (start > 0) {
            memmove(conn.rxBuffer, conn.rxBuffer + start, conn.rxBufferLen - start);
            conn.rxBufferLen -= start;
        }
        if (conn.rxBufferLen < JK_BMS_RESPONSE_SIZE) break;  // frame not complete yet

        BatteryData parsed;
        if (parseResponse(connIndex, conn.rxBuffer, parsed)) {
            mergeBatteryData(conn, parsed);
        }

        // Remove the frame, also when it carried no battery data (settings, device info)
        conn.rxBufferLen -= JK_BMS_RESPONSE_SIZE;
        memmove(conn.rxBuffer, conn.rxBuffer + JK_BMS_RESPONSE_SIZE, conn.rxBufferLen);
    }
}

// Parses one 300-byte response frame. Returns false when it carries no battery data (only
// cell info frames do). Unknown values are left at their BatteryData defaults, temperatures at NAN.
bool JKBmsBle::parseResponse(uint8_t connIndex, const uint8_t* frame, BatteryData& out) {
    BmsConnection& conn = _connections[connIndex];

    uint8_t expected = frame[JK_BMS_RESPONSE_SIZE - 1];
    uint8_t calculated = checksum(frame, JK_BMS_RESPONSE_SIZE - 1);
    if (calculated != expected) {
        ESP_LOGW(TAG, "[BMS%d] [PARSE] Checksum mismatch: exp 0x%02X got 0x%02X",
                 connIndex, expected, calculated);
    }

    // Any reply means the BMS answered DEVICE_INFO; requestData() switches to CELL_INFO after this
    conn.deviceInfoReceived = true;

    uint8_t frameType = frame[4];
    if (frameType != JK_BMS_FRAME_CELL_INFO) {
        // Settings (0x01) and device info (0x03) frames carry no live battery data
        ESP_LOGI(TAG, "[BMS%d] %s frame (0x%02X), ignored", conn.batteryIndex,
                 frameType == JK_BMS_FRAME_SETTINGS ? "Settings" :
                 frameType == JK_BMS_FRAME_DEVICE_INFO ? "Device info" : "Unknown", frameType);
        return false;
    }

    const uint8_t* p = frame + sizeof(RESPONSE_HEADER);
    const size_t len = JK_BMS_PAYLOAD_SIZE;
    out.temp1 = NAN;
    out.temp2 = NAN;

    // Cell info frame: the esphome jk_bms_ble JK02 cell info frame, in a 24-cell (JK02_24S) or
    // 32-cell (JK02_32S) variant.

    // 8 cell voltages at payload[2..17], uint16 mV
    float cells[8] = {0};
    uint8_t foundCells = 0;
    float packVoltage = 0;
    for (uint8_t i = 0; i < 8; i++) {
        uint16_t raw = u16At(p, 2 + i * 2);
        float v = raw * 0.001f;
        if (v >= 2.5f && v <= 4.5f) {
            cells[i] = v;
            packVoltage += v;
            foundCells++;
        } else if (raw == 0) {
            break;
        }
    }

    // Current. In the 32-cell variant everything after the cells is shifted by 32
    // (payload offset = esphome frame offset - 4):
    //   pack voltage  uint32 mV  payload 114 / 146
    //   current       int32  mA  payload 122 / 154  (positive = charging)
    // Pick the variant whose pack voltage matches the sum of the cell voltages.
    if (foundCells > 0) {
        for (size_t shift : {0, 32}) {
            float packV = u32At(p, 114 + shift) * 0.001f;
            if (fabsf(packV - packVoltage) < 0.5f) {
                float current = (int32_t) u32At(p, 122 + shift) * 0.001f;
                if (current >= -500.0f && current <= 500.0f) {
                    conn.cachedCurrent = current;
                }
                break;
            }
        }
    }
    out.current = conn.cachedCurrent;  // last known value if not found

    // SOC: at payload[137] (24-cell) or [169] (32-cell); payload[20] is usually a 0 placeholder.
    // Then the cached value, then the first byte that could be a percentage.
    auto socAt = [&](size_t off) -> int { return (p[off] > 0 && p[off] <= 100) ? p[off] : -1; };
    int soc = socAt(137);
    if (soc < 0) soc = socAt(169);
    if (soc < 0) soc = socAt(20);
    if (soc < 0 && conn.cachedSoc <= 100) soc = conn.cachedSoc;
    for (size_t off = 21; soc < 0 && off < len; off++) {
        if (p[off] <= 100) soc = p[off];
    }
    out.soc = soc < 0 ? 0 : soc;

    // Temperatures: no fixed offset known; search backwards from the end of the frame for plausible
    // values (e.g. 0x2C 0x01 = 30.0 °C near the end), widening the range if needed
    float temps[2] = {NAN, NAN};
    uint8_t foundTemps = 0;
    searchTemps(p, len, true, 15.0f, 45.0f, temps, foundTemps);
    searchTemps(p, len, true, 10.0f, 50.0f, temps, foundTemps);
    searchTemps(p, len, false, 10.0f, 50.0f, temps, foundTemps);
    out.temp1 = temps[0];
    out.temp2 = temps[1];

    // Cells not at the expected offset: look for 8 consecutive plausible cell voltages anywhere
    // (done after the current lookup above, which only trusts cells at the expected offset)
    if (foundCells == 0) {
        for (size_t off = 0; off + 16 <= len; off++) {
            float sum = 0;
            uint8_t valid = 0;
            for (uint8_t i = 0; i < 8; i++) {
                float v = u16At(p, off + i * 2) * 0.001f;
                if (v >= 2.5f && v <= 4.5f) {
                    sum += v;
                    valid++;
                }
            }
            if (valid >= 7) {
                foundCells = valid;
                packVoltage = sum;
                for (uint8_t i = 0; i < 8; i++) {
                    cells[i] = u16At(p, off + i * 2) * 0.001f;
                }
                break;
            }
        }
    }

    out.voltage = packVoltage;
    float minV = 5.0f, maxV = 0.0f;
    for (uint8_t i = 0; i < foundCells && i < 8; i++) {
        out.cellVoltages[i] = cells[i];
        if (cells[i] > 0) {
            minV = std::min(minV, cells[i]);
            maxV = std::max(maxV, cells[i]);
        }
    }
    out.cellDiff = maxV >= minV ? maxV - minV : 0.0f;
    return true;
}

// Merges a parsed cell info frame into _batteries
void JKBmsBle::mergeBatteryData(BmsConnection& conn, const BatteryData& parsed) {
    if (!_batteries || conn.batteryIndex >= _maxBatteries) return;

    xSemaphoreTake(_dataMutex, portMAX_DELAY);
    BatteryData& bat = _batteries[conn.batteryIndex];
    if (parsed.voltage > 0) bat.voltage = parsed.voltage;
    bat.current = parsed.current;  // 0 is valid (no load)
    for (uint8_t i = 0; i < 8; i++) {
        if (parsed.cellVoltages[i] > 0) bat.cellVoltages[i] = parsed.cellVoltages[i];
    }
    bat.cellDiff = parsed.cellDiff;
    if (parsed.soc >= 0 && parsed.soc <= 100) {
        bat.soc = parsed.soc;
        conn.cachedSoc = (uint8_t) parsed.soc;
    }
    if (validTemp(parsed.temp1)) bat.temp1 = parsed.temp1;
    if (validTemp(parsed.temp2)) bat.temp2 = parsed.temp2;
    bat.power = bat.voltage * bat.current;
    bat.timestamp = now_ms();
    BatteryData logged = bat;
    xSemaphoreGive(_dataMutex);

    // Log outside the lock
    ESP_LOGI(TAG, "[BMS%d] %s: V=%.2f I=%.2f SOC=%d%% T=%.1f/%.1f",
             conn.batteryIndex, logged.bmsName.c_str(), logged.voltage, logged.current,
             (int) logged.soc, logged.temp1, logged.temp2);
}
