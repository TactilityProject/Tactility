#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <bluetooth/esp32_ble_internal.h>
#include <bluetooth/esp32_ble_hid_host.h>

#include <tactility/Memory.hpp>
#include <tactility/concurrent/dispatcher.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/mouse.h>
#include <tactility/log.h>

#include <freertos/queue.h>

#include <nimble/nimble_port.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

constexpr auto* TAG = "esp32_ble_hid_host";

constexpr auto KEYBOARD_QUEUE_SIZE = 16;
// Left, right and middle, as in the boot protocol mouse report
constexpr uint8_t MOUSE_BUTTON_COUNT = 3;
constexpr auto WORKER_STACK_SIZE = 4096;
constexpr uint32_t ENC_RETRY_DELAY_MS = 500;

enum class HidReportType : uint8_t { Unknown = 0, Keyboard, Mouse, Consumer };

struct HidHostInputRpt {
    uint16_t valHandle;
    uint16_t cccdHandle;
    uint16_t rptRefHandle;
    uint8_t reportId;
    HidReportType type;
};

struct HidHostConnection {
    uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
    uint16_t hidSvcStart = 0;
    uint16_t hidSvcEnd = 0;
    std::vector<HidHostInputRpt> inputRpts;
    std::vector<uint16_t> allChrDefHandles;
    int subscribeIdx = 0;
    int dscDiscIdx = 0;
    int rptRefReadIdx = 0;
    uint16_t rptMapHandle = 0;
    std::vector<uint8_t> rptMap;
    bool securityInitiated = false;
    bool typeResolutionDone = false;
    bool readyBlockFired = false;
    uint8_t encRetryCount = 0;
    BtAddr peerAddr = {};
};

struct HidHostState {
    Device* bleDevice = nullptr;
    DispatcherHandle_t dispatcher = nullptr;
    Thread* worker = nullptr;
    std::atomic<bool> workerRunning{false};
    // Runs on the NimBLE host task, which owns the connection state. Initialized on the first connect, when the host is running.
    ble_npl_callout encRetryCallout {};
    bool encRetryCalloutReady = false;
    std::unique_ptr<HidHostConnection> connection;

    Device keyboardDevice = {};
    std::atomic<bool> keyboardActive{false};
    uint8_t prevKeys[6] = {};
    // The key published on press for each prevKeys slot, so release reuses the same mapping
    // (Shift etc. affect the pressed key's identity, the usage code alone doesn't).
    uint32_t prevPublishedKeys[6] = {};

    Device mouseDevice = {};
    std::atomic<bool> mouseActive{false};
    std::atomic<int32_t> mouseDx{0};
    std::atomic<int32_t> mouseDy{0};
    // Bit n is set while button n is pressed
    std::atomic<uint8_t> mouseButtons{0};
};

static HidHostState* s_state = nullptr;
// Set while the device stops. NimBLE still delivers callbacks then, such as the disconnects from stopping the host.
static std::atomic<bool> s_stopping {false};

static bool hostRunning() {
    return s_state != nullptr && !s_stopping.load();
}

// region Keyboard device

static error_t keyboardDeviceStart(Device* device) {
    auto* queue = xQueueCreate(KEYBOARD_QUEUE_SIZE, sizeof(KeyboardKeyData));
    if (queue == nullptr) {
        return ERROR_RESOURCE;
    }
    device_set_driver_data(device, queue);
    return ERROR_NONE;
}

static error_t keyboardDeviceStop(Device* device) {
    auto* queue = static_cast<QueueHandle_t>(device_get_driver_data(device));
    if (queue != nullptr) {
        vQueueDelete(queue);
    }
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

static error_t keyboardDeviceReadKey(Device* device, KeyboardKeyData* data) {
    auto* queue = static_cast<QueueHandle_t>(device_get_driver_data(device));
    if (queue == nullptr || xQueueReceive(queue, data, 0) != pdTRUE) {
        *data = {};
        return ERROR_NONE;
    }
    data->continue_reading = uxQueueMessagesWaiting(queue) > 0;
    return ERROR_NONE;
}

static const KeyboardApi keyboard_api = {
    .read_key = keyboardDeviceReadKey,
    .get_backlight = nullptr,
    .is_present = nullptr,
};

static Driver keyboard_driver = {
    .name = "esp32_ble_hid_host_keyboard",
    .compatible = (const char*[]) { nullptr },
    .start_device = keyboardDeviceStart,
    .stop_device = keyboardDeviceStop,
    .probe = nullptr,
    .api = &keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .internal = nullptr,
};

// endregion

// region Mouse device

static error_t mouseDeviceReadDelta(Device*, int32_t* out_dx, int32_t* out_dy) {
    *out_dx = s_state->mouseDx.exchange(0);
    *out_dy = s_state->mouseDy.exchange(0);
    return ERROR_NONE;
}

static error_t mouseDeviceGetButtonPressed(Device*, uint8_t index, bool* out_pressed) {
    *out_pressed = (s_state->mouseButtons.load() & (1 << index)) != 0;
    return ERROR_NONE;
}

static const MouseApi mouse_api = {
    .button_count = MOUSE_BUTTON_COUNT,
    .read_delta = mouseDeviceReadDelta,
    .get_button_pressed = mouseDeviceGetButtonPressed,
};

static Driver mouse_driver = {
    .name = "esp32_ble_hid_host_mouse",
    .compatible = (const char*[]) { nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &mouse_api,
    .device_type = &MOUSE_TYPE,
    .internal = nullptr,
};

// endregion

// region Dynamic child devices
// All of these run on the worker thread, because starting and stopping a device notifies listeners
// that bind it to LVGL, which must not happen on the NimBLE host task.

static void constructChildDevice(Device* child, const char* name, Driver* driver, std::atomic<bool>& active) {
    if (active.load()) {
        return;
    }

    *child = Device {
        .address = 0,
        .name = name,
        .config = nullptr,
        .parent = nullptr,
        .flags = 0,
        .internal = nullptr,
    };

    if (device_construct(child) != ERROR_NONE) {
        LOG_E(TAG, "failed to construct %s", name);
        return;
    }
    device_set_parent(child, s_state->bleDevice);
    device_set_driver(child, driver);
    if (device_add(child) != ERROR_NONE) {
        LOG_E(TAG, "failed to add %s", name);
        device_destruct(child);
        return;
    }
    if (device_start(child) != ERROR_NONE) {
        LOG_E(TAG, "failed to start %s", name);
        device_remove(child);
        device_destruct(child);
        return;
    }
    active.store(true);
}

static void destructChildDevice(Device* child, std::atomic<bool>& active) {
    if (!active.exchange(false)) {
        return;
    }
    device_stop(child);
    device_remove(child);
    device_destruct(child);
}

static void constructKeyboardTask(void*) {
    constructChildDevice(&s_state->keyboardDevice, "ble_keyboard0", &keyboard_driver, s_state->keyboardActive);
}

static void constructMouseTask(void*) {
    constructChildDevice(&s_state->mouseDevice, "ble_mouse0", &mouse_driver, s_state->mouseActive);
}

static void destructDevicesTask(void*) {
    destructChildDevice(&s_state->keyboardDevice, s_state->keyboardActive);
    destructChildDevice(&s_state->mouseDevice, s_state->mouseActive);
}

static int32_t workerMain(void*) {
    while (s_state->workerRunning.load()) {
        dispatcher_consume(s_state->dispatcher);
    }
    return 0;
}

static void stopWorkerTask(void*) {
    s_state->workerRunning.store(false);
}

// endregion

// region Events

static void fireProfileState(const BtAddr addr, BtProfileState state) {
    BtEvent event = {};
    event.type = BT_EVENT_PROFILE_STATE_CHANGED;
    memcpy(event.profile_state.addr, addr, BT_ADDR_LEN);
    event.profile_state.state = state;
    event.profile_state.profile = BT_PROFILE_HID_HOST;
    ble_publish_event(s_state->bleDevice, event);
}

static void setHostActive(bool active) {
    ble_get_ctx(s_state->bleDevice)->hid_host_active.store(active);
}

// endregion

// region Report handling

static void publishKey(uint32_t key, bool pressed, bool ctrl, bool alt, uint8_t hid_keycode, uint8_t hid_modifier) {
    if (!s_state->keyboardActive.load()) {
        return;
    }
    auto* queue = static_cast<QueueHandle_t>(device_get_driver_data(&s_state->keyboardDevice));
    if (queue == nullptr) {
        return;
    }
    KeyboardKeyData data = {};
    data.key = key;
    data.pressed = pressed;
    data.ctrl = ctrl;
    data.alt = alt;
    data.hid_keycode = hid_keycode;
    data.hid_modifier = hid_modifier;
    xQueueSend(queue, &data, 0);
}

// Caps/Num Lock state is not tracked, because that needs the HID output report (LEDs) over GATT.
static void handleKeyboardReport(const uint8_t* data, uint16_t len) {
    // Boot-protocol style report: [0]=modifier, [1]=reserved, [2..]=usage codes.
    if (len < 3 || !s_state->keyboardActive.load()) {
        return;
    }
    auto& state = *s_state;
    uint8_t mod = data[0];
    const uint8_t* curr = &data[2];
    int nkeys = std::min((int)(len - 2), 6);
    bool ctrl = (mod & KEYBOARD_HID_MOD_LEFT_CTRL) || (mod & KEYBOARD_HID_MOD_RIGHT_CTRL);
    bool alt = (mod & KEYBOARD_HID_MOD_LEFT_ALT) || (mod & KEYBOARD_HID_MOD_RIGHT_ALT);

    uint32_t next_published_keys[6] = {};

    for (int i = 0; i < 6; i++) {
        uint8_t kc = state.prevKeys[i];
        if (kc == 0) continue;
        bool still = false;
        for (int j = 0; j < nkeys; j++) {
            if (curr[j] == kc) {
                still = true;
                break;
            }
        }
        if (!still) {
            uint32_t key = state.prevPublishedKeys[i];
            if (key) publishKey(key, false, ctrl, alt, kc, mod);
        }
    }
    for (int i = 0; i < nkeys; i++) {
        uint8_t kc = curr[i];
        if (kc == 0) continue;
        int prev_idx = -1;
        for (int j = 0; j < 6; j++) {
            if (state.prevKeys[j] == kc) {
                prev_idx = j;
                break;
            }
        }
        uint32_t key;
        if (prev_idx < 0) {
            key = keyboard_key_from_hid_usage(mod, kc, false, false);
            if (key) publishKey(key, true, ctrl, alt, kc, mod);
        } else {
            key = state.prevPublishedKeys[prev_idx];
        }
        next_published_keys[i] = key;
    }
    std::memcpy(state.prevKeys, curr, nkeys);
    if (nkeys < 6) std::memset(state.prevKeys + nkeys, 0, 6 - nkeys);
    std::memcpy(state.prevPublishedKeys, next_published_keys, sizeof(next_published_keys));
}

static void handleMouseReport(const uint8_t* data, uint16_t len) {
    if (len < 3) return;
    if (!s_state->mouseActive.load()) {
        dispatcher_dispatch(s_state->dispatcher, nullptr, constructMouseTask);
    }
    s_state->mouseButtons.store(data[0] & ((1 << MOUSE_BUTTON_COUNT) - 1));
    s_state->mouseDx.fetch_add((int8_t)data[1]);
    s_state->mouseDy.fetch_add((int8_t)data[2]);
}

static void resetInputState() {
    std::memset(s_state->prevKeys, 0, sizeof(s_state->prevKeys));
    std::memset(s_state->prevPublishedKeys, 0, sizeof(s_state->prevPublishedKeys));
    s_state->mouseDx.store(0);
    s_state->mouseDy.store(0);
    s_state->mouseButtons.store(0);
}

// endregion

// ---- Forward declarations ----

static void hidHostSubscribeNext(HidHostConnection& conn);
static void hidHostStartRptRefRead(HidHostConnection& conn);
static void hidHostReadReportMap(HidHostConnection& conn);
static uint16_t getDescEndHandle(const HidHostConnection& conn, uint16_t valHandle);

// region Encryption retry timer

static void startEncRetryTimer() {
    ble_npl_callout_reset(&s_state->encRetryCallout, ble_npl_time_ms_to_ticks32(ENC_RETRY_DELAY_MS));
}

static void encRetryTimerCb(ble_npl_event*) {
    if (!hostRunning() || !s_state->connection) {
        return;
    }
    auto& conn = *s_state->connection;
    if (!conn.typeResolutionDone) {
        // Discovery is still in flight. Forcing the subscribe and ready block early would fire it
        // with an empty report list and never create a keyboard device. Wait (bounded) for
        // inputRpts to populate, then only the quick report map read is left so retries are capped lower.
        bool have_reports = !conn.inputRpts.empty();
        int retry_cap = have_reports ? 6 : 40; // ~3s vs ~20s
        if (conn.encRetryCount < retry_cap) {
            conn.encRetryCount++;
            startEncRetryTimer();
            return;
        }
        LOG_W(TAG, "Discovery stalled after %d retries, proceeding with %d reports",
              (int)conn.encRetryCount, (int)conn.inputRpts.size());
        conn.typeResolutionDone = true;
        conn.subscribeIdx = 0;
    } else {
        LOG_I(TAG, "Post-encryption delay complete, starting CCCD subscriptions");
    }
    hidHostSubscribeNext(conn);
}

static void stopEncRetryTimer() {
    if (s_state->encRetryCalloutReady) {
        ble_npl_callout_stop(&s_state->encRetryCallout);
    }
}

// endregion

// region Report map parsing

static void applyReportMapTypes(HidHostConnection& conn) {
    const uint8_t* data = conn.rptMap.data();
    size_t len = conn.rptMap.size();

    uint16_t usagePage = 0, usage = 0;
    uint8_t reportId = 0;
    int depth = 0;
    HidReportType collType = HidReportType::Unknown;

    struct Entry { uint8_t id; HidReportType type; };
    std::vector<Entry> typeMap;
    std::vector<HidReportType> collOrder;
    bool collHadInput = false;

    size_t i = 0;
    while (i < len) {
        uint8_t prefix = data[i++];
        if (prefix == 0xFE) {
            if (i + 1 >= len) break;
            uint8_t lsz = data[i++];
            i++;
            i += lsz;
            continue;
        }
        uint8_t bSize = prefix & 0x03;
        uint8_t bType = (prefix >> 2) & 0x03;
        uint8_t bTag = (prefix >> 4) & 0x0F;
        uint8_t dataLen = (bSize == 3) ? 4 : bSize;
        if (i + dataLen > len) break;
        uint32_t value = 0;
        for (uint8_t j = 0; j < dataLen; j++) value |= (uint32_t)data[i++] << (8 * j);

        if (bType == 0) {
            if (bTag == 0xA) {
                if (depth == 0 && value == 0x01) {
                    if (usagePage == 0x01 && usage == 0x06) collType = HidReportType::Keyboard;
                    else if (usagePage == 0x01 && usage == 0x02) collType = HidReportType::Mouse;
                    else if (usagePage == 0x0C) collType = HidReportType::Consumer;
                    else collType = HidReportType::Unknown;
                    collHadInput = false;
                }
                depth++;
                usage = 0;
            } else if (bTag == 0xC) {
                if (depth > 0) depth--;
                if (depth == 0) {
                    collType = HidReportType::Unknown;
                    collHadInput = false;
                }
                usage = 0;
            } else if (bTag == 0x8) {
                if (depth > 0 && collType != HidReportType::Unknown) {
                    if (!collHadInput) {
                        collOrder.push_back(collType);
                        collHadInput = true;
                    }
                    if (reportId != 0) {
                        bool found = false;
                        for (const auto& e : typeMap) {
                            if (e.id == reportId) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) typeMap.push_back({reportId, collType});
                    }
                }
                usage = 0;
            } else {
                usage = 0;
            }
        } else if (bType == 1) {
            if (bTag == 0x0) usagePage = (uint16_t)value;
            else if (bTag == 0x8) reportId = (uint8_t)value;
        } else if (bType == 2) {
            if (bTag == 0x0) usage = (uint16_t)value;
        }
    }

    bool anyNonZeroId = false;
    for (const auto& rpt : conn.inputRpts) {
        if (rpt.reportId != 0) {
            anyNonZeroId = true;
            break;
        }
    }

    size_t zeroRptIdx = 0;
    for (auto& rpt : conn.inputRpts) {
        if (anyNonZeroId) {
            for (const auto& e : typeMap) {
                if (e.id == rpt.reportId) {
                    rpt.type = e.type;
                    break;
                }
            }
        } else {
            if (zeroRptIdx < collOrder.size()) rpt.type = collOrder[zeroRptIdx];
            zeroRptIdx++;
        }
        LOG_I(TAG, "Report val_handle=%d reportId=%d type=%d", rpt.valHandle, rpt.reportId, (int)rpt.type);
    }
    conn.rptMap.clear();
}

// endregion

// region Report reference read chain

static void hidHostStartRptRefRead(HidHostConnection& conn) {
    while (conn.rptRefReadIdx < (int)conn.inputRpts.size() &&
           conn.inputRpts[conn.rptRefReadIdx].rptRefHandle == 0) {
        conn.rptRefReadIdx++;
    }
    if (conn.rptRefReadIdx >= (int)conn.inputRpts.size()) {
        hidHostReadReportMap(conn);
        return;
    }
    uint16_t handle = conn.inputRpts[conn.rptRefReadIdx].rptRefHandle;
    int rc = ble_gattc_read(conn.connHandle, handle, [](uint16_t conn_handle,
                             const struct ble_gatt_error* error,
                             struct ble_gatt_attr* attr, void*) -> int {
        if (!hostRunning() || !s_state->connection) return 0;
        auto& conn = *s_state->connection;
        if (conn_handle != conn.connHandle) return 0;
        if (error->status == BLE_HS_EDONE) return 0;
        if (error->status == 0 && attr != nullptr) {
            if (OS_MBUF_PKTLEN(attr->om) >= 2 && conn.rptRefReadIdx < (int)conn.inputRpts.size()) {
                uint8_t rpt_ref[2] = {};
                os_mbuf_copydata(attr->om, 0, 2, rpt_ref);
                conn.inputRpts[conn.rptRefReadIdx].reportId = rpt_ref[0];
                LOG_I(TAG, "Report[%d] val_handle=%d reportId=%d", conn.rptRefReadIdx,
                      conn.inputRpts[conn.rptRefReadIdx].valHandle, rpt_ref[0]);
            }
        }
        conn.rptRefReadIdx++;
        hidHostStartRptRefRead(conn);
        return 0;
    }, nullptr);
    if (rc != 0) {
        LOG_W(TAG, "rptRef read[%d] failed rc=%d, skipping", conn.rptRefReadIdx, rc);
        conn.rptRefReadIdx++;
        hidHostStartRptRefRead(conn);
    }
}

// endregion

// region Report map read

static void hidHostReadReportMap(HidHostConnection& conn) {
    if (conn.rptMapHandle == 0) {
        LOG_I(TAG, "No Report Map char, skipping type resolution");
        conn.typeResolutionDone = true;
        conn.subscribeIdx = 0;
        hidHostSubscribeNext(conn);
        return;
    }
    int rc = ble_gattc_read_long(conn.connHandle, conn.rptMapHandle, 0,
        [](uint16_t conn_handle, const struct ble_gatt_error* error,
           struct ble_gatt_attr* attr, void*) -> int {
            if (!hostRunning() || !s_state->connection) return 0;
            auto& conn = *s_state->connection;
            if (conn_handle != conn.connHandle) return 0;
            if (error->status == 0 && attr != nullptr) {
                uint16_t chunk = OS_MBUF_PKTLEN(attr->om);
                size_t old_sz = conn.rptMap.size();
                conn.rptMap.resize(old_sz + chunk);
                os_mbuf_copydata(attr->om, 0, chunk, conn.rptMap.data() + old_sz);
                return 0;
            }
            if (!conn.rptMap.empty()) {
                LOG_I(TAG, "Report map read (%d bytes)", (int)conn.rptMap.size());
                applyReportMapTypes(conn);
            } else {
                LOG_W(TAG, "Report map read failed, types remain Unknown");
            }
            conn.typeResolutionDone = true;
            conn.subscribeIdx = 0;
            hidHostSubscribeNext(conn);
            return 0;
        }, nullptr);
    if (rc != 0) {
        LOG_W(TAG, "Report map read_long failed rc=%d, skipping", rc);
        conn.typeResolutionDone = true;
        conn.subscribeIdx = 0;
        hidHostSubscribeNext(conn);
    }
}

// endregion

// region CCCD subscription chain

static int hidHostCccdWriteCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                              struct ble_gatt_attr*, void*) {
    if (!hostRunning() || !s_state->connection) return 0;
    auto& conn = *s_state->connection;
    if (conn_handle != conn.connHandle) return 0;

    if (error->status != 0 && error->status != BLE_HS_EDONE) {
        if ((error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
             error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC))
            && !conn.securityInitiated) {
            LOG_I(TAG, "CCCD auth required, initiating security");
            conn.securityInitiated = true;
            ble_gap_security_initiate(conn_handle);
            return 0;
        }
        if (error->status == BLE_HS_ETIMEOUT) {
            LOG_W(TAG, "CCCD write timed out for report[%d], skipping", conn.subscribeIdx);
            conn.subscribeIdx++;
            hidHostSubscribeNext(conn);
            return 0;
        }
        if (error->status == BLE_HS_ENOTCONN) {
            LOG_W(TAG, "CCCD write failed, not connected");
            return 0;
        }
        LOG_W(TAG, "CCCD write failed status=%d", error->status);
    }
    conn.subscribeIdx++;
    hidHostSubscribeNext(conn);
    return 0;
}

static void hidHostSubscribeNext(HidHostConnection& conn) {
    if (conn.subscribeIdx >= (int)conn.inputRpts.size()) {
        if (conn.readyBlockFired) {
            LOG_I(TAG, "Subscribe ready block already ran, ignoring duplicate");
            return;
        }
        conn.readyBlockFired = true;
        LOG_I(TAG, "All %d reports subscribed, ready", (int)conn.inputRpts.size());
        stopEncRetryTimer();

        // "Unknown" report types (report map read failed) count as keyboard, because the
        // notification handler routes 6+ byte reports of unknown type to the keyboard path.
        bool has_keyboard_report = false;
        bool has_mouse_report = false;
        for (const auto& rpt : conn.inputRpts) {
            if (rpt.type == HidReportType::Keyboard || rpt.type == HidReportType::Unknown) {
                has_keyboard_report = true;
            } else if (rpt.type == HidReportType::Mouse) {
                has_mouse_report = true;
            }
        }
        if (has_keyboard_report) {
            dispatcher_dispatch(s_state->dispatcher, nullptr, constructKeyboardTask);
        }
        if (has_mouse_report) {
            dispatcher_dispatch(s_state->dispatcher, nullptr, constructMouseTask);
        }

        fireProfileState(conn.peerAddr, BT_PROFILE_STATE_CONNECTED);
        return;
    }
    auto& rpt = conn.inputRpts[conn.subscribeIdx];
    if (rpt.cccdHandle == 0) {
        conn.subscribeIdx++;
        hidHostSubscribeNext(conn);
        return;
    }
    static const uint16_t notify_val = 0x0001;
    int rc = ble_gattc_write_flat(conn.connHandle, rpt.cccdHandle,
                                  &notify_val, sizeof(notify_val),
                                  hidHostCccdWriteCb, nullptr);
    if (rc != 0) {
        LOG_W(TAG, "gattc_write_flat CCCD failed rc=%d", rc);
        conn.subscribeIdx++;
        hidHostSubscribeNext(conn);
    }
}

// endregion

// region Descriptor discovery

static int hidHostDscDiscCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                            uint16_t chr_val_handle, const struct ble_gatt_dsc* dsc, void*) {
    if (!hostRunning() || !s_state->connection) return 0;
    auto& conn = *s_state->connection;
    if (conn_handle != conn.connHandle) return 0;

    if (error->status == 0 && dsc != nullptr) {
        uint16_t dsc_uuid = ble_uuid_u16(&dsc->uuid.u);
        for (auto& rpt : conn.inputRpts) {
            if (rpt.valHandle != chr_val_handle) continue;
            if (dsc_uuid == 0x2902) {
                rpt.cccdHandle = dsc->handle;
            } else if (dsc_uuid == 0x2908) {
                rpt.rptRefHandle = dsc->handle;
            }
            break;
        }
    } else if (error->status != 0) {
        // A failed descriptor discovery skips to the next report, like a finished one
        if (error->status != BLE_HS_EDONE) {
            LOG_W(TAG, "disc_all_dscs[%d] status=%d", conn.dscDiscIdx, error->status);
        }
        int next_idx = conn.dscDiscIdx + 1;
        if (next_idx < (int)conn.inputRpts.size()) {
            conn.dscDiscIdx = next_idx;
            auto& next_rpt = conn.inputRpts[next_idx];
            uint16_t end = getDescEndHandle(conn, next_rpt.valHandle);
            int rc = ble_gattc_disc_all_dscs(conn.connHandle, next_rpt.valHandle, end,
                                             hidHostDscDiscCb, nullptr);
            if (rc != 0) {
                LOG_W(TAG, "disc_all_dscs[%d] failed rc=%d", next_idx, rc);
                conn.rptRefReadIdx = 0;
                hidHostStartRptRefRead(conn);
            }
        } else {
            conn.rptRefReadIdx = 0;
            hidHostStartRptRefRead(conn);
        }
    }
    return 0;
}

static uint16_t getDescEndHandle(const HidHostConnection& conn, uint16_t valHandle) {
    for (uint16_t dh : conn.allChrDefHandles) {
        if (dh > valHandle) return dh - 1;
    }
    return conn.hidSvcEnd;
}

// endregion

// region Characteristic and service discovery

static int hidHostChrDiscCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                            const struct ble_gatt_chr* chr, void*) {
    if (!hostRunning() || !s_state->connection) return 0;
    auto& conn = *s_state->connection;
    if (conn_handle != conn.connHandle) return 0;

    if (error->status == 0 && chr != nullptr) {
        conn.allChrDefHandles.push_back(chr->def_handle);
        uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);
        if (uuid16 == 0x2A4D && (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
            HidHostInputRpt rpt = {};
            rpt.valHandle = chr->val_handle;
            conn.inputRpts.push_back(rpt);
            LOG_I(TAG, "Input Report chr val_handle=%d", chr->val_handle);
        } else if (uuid16 == 0x2A4B) {
            conn.rptMapHandle = chr->val_handle;
        }
    } else if (error->status != 0 && error->status != BLE_HS_EDONE) {
        LOG_W(TAG, "disc_all_chrs status=%d, disconnecting", error->status);
        ble_gap_terminate(conn.connHandle, BLE_ERR_REM_USER_CONN_TERM);
    } else if (error->status == BLE_HS_EDONE) {
        std::sort(conn.allChrDefHandles.begin(), conn.allChrDefHandles.end());
        if (conn.inputRpts.empty()) {
            LOG_W(TAG, "No Input Report chars, disconnecting");
            ble_gap_terminate(conn.connHandle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        conn.dscDiscIdx = 0;
        auto& first = conn.inputRpts[0];
        uint16_t end = getDescEndHandle(conn, first.valHandle);
        int rc = ble_gattc_disc_all_dscs(conn.connHandle, first.valHandle, end,
                                         hidHostDscDiscCb, nullptr);
        if (rc != 0) {
            LOG_W(TAG, "disc_all_dscs[0] failed rc=%d", rc);
            conn.rptRefReadIdx = 0;
            hidHostStartRptRefRead(conn);
        }
    }
    return 0;
}

static int hidHostSvcDiscCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                            const struct ble_gatt_svc* svc, void*) {
    if (!hostRunning() || !s_state->connection) return 0;
    auto& conn = *s_state->connection;
    if (conn_handle != conn.connHandle) return 0;

    if (error->status == 0 && svc != nullptr) {
        if (ble_uuid_u16(&svc->uuid.u) == 0x1812) {
            conn.hidSvcStart = svc->start_handle;
            conn.hidSvcEnd = svc->end_handle;
            LOG_I(TAG, "HID service start=%d end=%d", conn.hidSvcStart, conn.hidSvcEnd);
        }
    } else if (error->status != 0 && error->status != BLE_HS_EDONE) {
        LOG_W(TAG, "disc_all_svcs status=%d, disconnecting", error->status);
        ble_gap_terminate(conn.connHandle, BLE_ERR_REM_USER_CONN_TERM);
    } else if (error->status == BLE_HS_EDONE) {
        if (conn.hidSvcStart == 0) {
            LOG_W(TAG, "No HID service found, disconnecting");
            ble_gap_terminate(conn.connHandle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        int rc = ble_gattc_disc_all_chrs(conn.connHandle, conn.hidSvcStart, conn.hidSvcEnd,
                                         hidHostChrDiscCb, nullptr);
        if (rc != 0) {
            LOG_W(TAG, "disc_all_chrs failed rc=%d", rc);
            ble_gap_terminate(conn.connHandle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    return 0;
}

// endregion

// region GAP

static void onConnectionEnded() {
    BtAddr addr;
    memcpy(addr, s_state->connection->peerAddr, BT_ADDR_LEN);
    stopEncRetryTimer();
    s_state->connection.reset();
    resetInputState();
    setHostActive(false);
    fireProfileState(addr, BT_PROFILE_STATE_IDLE);
    dispatcher_dispatch(s_state->dispatcher, nullptr, destructDevicesTask);
}

static int hidHostGapCb(struct ble_gap_event* event, void*) {
    if (!hostRunning() || !s_state->connection) return 0;
    auto& conn = *s_state->connection;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                conn.connHandle = event->connect.conn_handle;
                LOG_I(TAG, "Connected (handle=%d)", conn.connHandle);
                int rc = ble_gattc_disc_all_svcs(conn.connHandle, hidHostSvcDiscCb, nullptr);
                if (rc != 0) {
                    LOG_W(TAG, "disc_all_svcs failed rc=%d", rc);
                    ble_gap_terminate(conn.connHandle, BLE_ERR_REM_USER_CONN_TERM);
                }
            } else {
                LOG_W(TAG, "Connect failed status=%d", event->connect.status);
                onConnectionEnded();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            LOG_I(TAG, "Disconnected reason=%d", event->disconnect.reason);
            onConnectionEnded();
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.conn_handle == conn.connHandle) {
                if (event->enc_change.status == 0) {
                    LOG_I(TAG, "Encryption established, retrying CCCD in 500ms");
                    conn.subscribeIdx = 0;
                    if (s_state->encRetryCalloutReady) {
                        startEncRetryTimer();
                    } else {
                        hidHostSubscribeNext(conn);
                    }
                } else {
                    LOG_W(TAG, "Encryption failed status=%d", event->enc_change.status);
                }
            }
            break;

        case BLE_GAP_EVENT_NOTIFY_RX:
            if (event->notify_rx.conn_handle == conn.connHandle) {
                uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
                if (len > 0 && len <= 64) {
                    uint8_t buf[64] = {};
                    os_mbuf_copydata(event->notify_rx.om, 0, len, buf);
                    for (const auto& rpt : conn.inputRpts) {
                        if (rpt.valHandle != event->notify_rx.attr_handle) continue;
                        switch (rpt.type) {
                            case HidReportType::Keyboard: handleKeyboardReport(buf, len); break;
                            case HidReportType::Mouse: handleMouseReport(buf, len); break;
                            case HidReportType::Consumer:
                                LOG_I(TAG, "Consumer report len=%d", len);
                                break;
                            case HidReportType::Unknown:
                                if (len >= 6) handleKeyboardReport(buf, len);
                                else if (len >= 3) handleMouseReport(buf, len);
                                break;
                        }
                        break;
                    }
                }
            }
            break;

        default:
            break;
    }
    return 0;
}

// endregion

// region Public functions

static ble_addr_t lookupPeerAddress(BleCtx* ctx, const BtAddr addr) {
    ble_addr_t ble_addr = {};
    ble_addr.type = BLE_ADDR_PUBLIC;
    memcpy(ble_addr.val, addr, BT_ADDR_LEN);

    xSemaphoreTake(ctx->scan_mutex, portMAX_DELAY);
    for (size_t i = 0; i < ctx->scan_count; i++) {
        if (memcmp(ctx->scan_results[i].addr, addr, BT_ADDR_LEN) == 0) {
            ble_addr.type = ctx->scan_addrs[i].type;
            break;
        }
    }
    xSemaphoreGive(ctx->scan_mutex);
    return ble_addr;
}

error_t ble_hid_host_init(Device* device) {
    auto* state = tt::newObject<HidHostState>();
    if (state == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    state->bleDevice = device;
    state->dispatcher = dispatcher_alloc();
    state->workerRunning.store(true);
    state->worker = thread_alloc_full("ble_hid_host", WORKER_STACK_SIZE, workerMain, nullptr, tskNO_AFFINITY);
    s_state = state;
    if (state->dispatcher == nullptr || state->worker == nullptr || thread_start(state->worker) != ERROR_NONE) {
        LOG_E(TAG, "failed to start worker");
        s_state = nullptr;
        if (state->worker != nullptr) thread_free(state->worker);
        if (state->dispatcher != nullptr) dispatcher_free(state->dispatcher);
        tt::deleteObject(state);
        return ERROR_RESOURCE;
    }

    if (driver_construct(&keyboard_driver) != ERROR_NONE || driver_construct(&mouse_driver) != ERROR_NONE) {
        LOG_E(TAG, "failed to construct drivers");
    }

    return ERROR_NONE;
}

void ble_hid_host_deinit() {
    if (s_state == nullptr) {
        return;
    }
    auto* state = s_state;
    if (state->encRetryCalloutReady) {
        ble_npl_callout_stop(&state->encRetryCallout);
        ble_npl_callout_deinit(&state->encRetryCallout);
    }
    s_stopping.store(true);
    if (state->connection) {
        if (state->connection->connHandle != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(state->connection->connHandle, BLE_ERR_REM_USER_CONN_TERM);
        } else {
            ble_gap_conn_cancel();
        }
    }
    state->connection.reset();
    dispatcher_dispatch(state->dispatcher, nullptr, destructDevicesTask);
    dispatcher_dispatch(state->dispatcher, nullptr, stopWorkerTask);
    thread_join(state->worker, portMAX_DELAY, 10);
    thread_free(state->worker);
    dispatcher_free(state->dispatcher);
    driver_destruct(&mouse_driver);
    driver_destruct(&keyboard_driver);
    s_state = nullptr;
    s_stopping.store(false);
    tt::deleteObject(state);
}

error_t ble_hid_host_connect(Device* device, const BtAddr addr) {
    if (!hostRunning()) {
        return ERROR_INVALID_STATE;
    }
    BleCtx* ctx = ble_get_ctx(device);
    if (ctx->radio_state.load() != BT_RADIO_STATE_ON) {
        LOG_W(TAG, "connect: radio not on");
        return ERROR_INVALID_STATE;
    }
    if (s_state->connection) {
        LOG_W(TAG, "connect: already connecting or connected");
        return ERROR_INVALID_STATE;
    }

    if (!s_state->encRetryCalloutReady) {
        ble_npl_callout_init(&s_state->encRetryCallout, nimble_port_get_dflt_eventq(), encRetryTimerCb, nullptr);
        s_state->encRetryCalloutReady = true;
    }

    resetInputState();
    s_state->connection = std::make_unique<HidHostConnection>();
    memcpy(s_state->connection->peerAddr, addr, BT_ADDR_LEN);
    setHostActive(true);

    ble_addr_t ble_addr = lookupPeerAddress(ctx, addr);
    // Central role always uses the public address, so bonds with HID hosts survive identity
    // changes.
    const uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;

    // ble_gap_connect() is rejected while discovery is active
    if (ble_gap_disc_active()) {
        ble_gap_disc_cancel();
        ble_set_scan_active(device, false);
        BtEvent scan_finished = {};
        scan_finished.type = BT_EVENT_SCAN_FINISHED;
        ble_publish_event(device, scan_finished);
    }

    int rc = ble_gap_connect(own_addr_type, &ble_addr, 5000, nullptr, hidHostGapCb, nullptr);
    if (rc != 0) {
        LOG_W(TAG, "ble_gap_connect failed rc=%d", rc);
        // Fires IDLE so the service can start a new scan and retry
        onConnectionEnded();
        return ERROR_RESOURCE;
    }
    LOG_I(TAG, "Connecting...");
    fireProfileState(addr, BT_PROFILE_STATE_CONNECTING);
    return ERROR_NONE;
}

error_t ble_hid_host_disconnect() {
    if (s_state == nullptr || !s_state->connection || s_state->connection->connHandle == BLE_HS_CONN_HANDLE_NONE) {
        return ERROR_INVALID_STATE;
    }
    ble_gap_terminate(s_state->connection->connHandle, BLE_ERR_REM_USER_CONN_TERM);
    return ERROR_NONE;
}

// endregion

#endif // CONFIG_BT_NIMBLE_ENABLED
