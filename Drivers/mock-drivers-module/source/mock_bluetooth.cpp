// SPDX-License-Identifier: Apache-2.0

// Mock Bluetooth driver for the simulator: fakes a radio with a fixed list of nearby peers
// instead of talking to real hardware. Actions succeed instantly, there's no real scan, pairing
// or connection delay.

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/log.h>

#include <cstring>
#include <new>

#define TAG "mock_bluetooth"

namespace {

struct MockPeerFixture {
    BtAddr addr;
    const char* name;
    int8_t rssi;
};

constexpr MockPeerFixture MOCK_PEERS[] = {
    { { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 }, "Mock Keyboard", -45 },
    { { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 }, "Mock Gamepad", -60 },
    { { 0x02, 0x00, 0x00, 0x00, 0x00, 0x03 }, "Mock Speaker", -75 },
};
constexpr size_t MOCK_PEER_COUNT = sizeof(MOCK_PEERS) / sizeof(MOCK_PEERS[0]);
constexpr const char* MOCK_DEFAULT_NAME = "Tactility";

struct MockPeerState {
    bool paired = false;
    /** Bit per BtProfileId */
    uint8_t connectedProfiles = 0;
};

struct MockBluetoothCtx {
    Device* device = nullptr;

    Mutex mutex {};
    BtRadioState radioState = BT_RADIO_STATE_OFF;
    bool scanning = false;
    char deviceName[BLE_DEVICE_NAME_MAX + 1] = {};
    /** Empty means "use deviceName". */
    char identity[BLE_IDENTITY_MAX + 1] = {};
    MockPeerState peers[MOCK_PEER_COUNT];

    Mutex subscriptionsMutex {};
    BtEventSubscription* subscriptions = nullptr;
};

#define GET_CTX(device) (static_cast<MockBluetoothCtx*>(device_get_driver_data(device)))

void fireEvent(MockBluetoothCtx* ctx, const BtEvent& event) {
    mutex_lock(&ctx->subscriptionsMutex);
    for (BtEventSubscription* sub = ctx->subscriptions; sub != nullptr; sub = sub->internal.next) {
        mutex_lock(&sub->internal.ring_mutex);
        if (sub->internal.count < BT_EVENT_QUEUE_CAPACITY) {
            uint8_t tail = (sub->internal.head + sub->internal.count) % BT_EVENT_QUEUE_CAPACITY;
            sub->internal.queue[tail] = event;
            sub->internal.count++;
        }
        mutex_unlock(&sub->internal.ring_mutex);
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
    mutex_unlock(&ctx->subscriptionsMutex);
}

// Returns the index of the peer, or MOCK_PEER_COUNT when the address isn't a mock peer
size_t findPeer(const BtAddr addr) {
    for (size_t i = 0; i < MOCK_PEER_COUNT; i++) {
        if (memcmp(MOCK_PEERS[i].addr, addr, BT_ADDR_LEN) == 0) {
            return i;
        }
    }
    return MOCK_PEER_COUNT;
}

// Caller holds ctx->mutex
void fillPeerRecord(const MockBluetoothCtx* ctx, size_t index, BtPeerRecord* record) {
    memset(record, 0, sizeof(*record));
    memcpy(record->addr, MOCK_PEERS[index].addr, BT_ADDR_LEN);
    strncpy(record->name, MOCK_PEERS[index].name, BT_NAME_MAX);
    record->rssi = MOCK_PEERS[index].rssi;
    record->paired = ctx->peers[index].paired;
    record->connected = ctx->peers[index].connectedProfiles != 0;
}

void fireRadioState(MockBluetoothCtx* ctx, BtRadioState state) {
    BtEvent event = {};
    event.type = BT_EVENT_RADIO_STATE_CHANGED;
    event.radio_state = state;
    fireEvent(ctx, event);
}

void fireProfileState(MockBluetoothCtx* ctx, size_t index, BtProfileId profile, BtProfileState state) {
    BtEvent event = {};
    event.type = BT_EVENT_PROFILE_STATE_CHANGED;
    memcpy(event.profile_state.addr, MOCK_PEERS[index].addr, BT_ADDR_LEN);
    event.profile_state.profile = profile;
    event.profile_state.state = state;
    fireEvent(ctx, event);
}

void fireConnectState(MockBluetoothCtx* ctx, size_t index) {
    BtEvent event = {};
    event.type = BT_EVENT_CONNECT_STATE_CHANGED;
    mutex_lock(&ctx->mutex);
    fillPeerRecord(ctx, index, &event.peer);
    mutex_unlock(&ctx->mutex);
    fireEvent(ctx, event);
}

// ---- BluetoothApi ----

error_t apiGetRadioState(Device* device, BtRadioState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || state == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    *state = ctx->radioState;
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t apiSetRadioEnabled(Device* device, bool enabled) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    const bool changed = enabled != (ctx->radioState == BT_RADIO_STATE_ON);
    ctx->radioState = enabled ? BT_RADIO_STATE_ON : BT_RADIO_STATE_OFF;
    bool disconnected[MOCK_PEER_COUNT] = {};
    if (!enabled) {
        ctx->scanning = false;
        for (size_t i = 0; i < MOCK_PEER_COUNT; i++) {
            disconnected[i] = ctx->peers[i].connectedProfiles != 0;
            ctx->peers[i].connectedProfiles = 0;
        }
    }
    mutex_unlock(&ctx->mutex);
    if (!changed) return ERROR_NONE;

    for (size_t i = 0; i < MOCK_PEER_COUNT; i++) {
        if (disconnected[i]) fireConnectState(ctx, i);
    }
    fireRadioState(ctx, enabled ? BT_RADIO_STATE_ON : BT_RADIO_STATE_OFF);

    LOG_I(TAG, "Bluetooth radio %s (mock)", enabled ? "on" : "off");
    return ERROR_NONE;
}

error_t apiScanStart(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    if (ctx->radioState != BT_RADIO_STATE_ON || ctx->scanning) {
        mutex_unlock(&ctx->mutex);
        return ERROR_INVALID_STATE;
    }
    ctx->scanning = true;
    mutex_unlock(&ctx->mutex);

    BtEvent started_event = {};
    started_event.type = BT_EVENT_SCAN_STARTED;
    fireEvent(ctx, started_event);

    for (size_t i = 0; i < MOCK_PEER_COUNT; i++) {
        BtEvent found_event = {};
        found_event.type = BT_EVENT_PEER_FOUND;
        mutex_lock(&ctx->mutex);
        fillPeerRecord(ctx, i, &found_event.peer);
        mutex_unlock(&ctx->mutex);
        fireEvent(ctx, found_event);
    }

    // No real radio, so the "scan" completes instantly.
    mutex_lock(&ctx->mutex);
    ctx->scanning = false;
    mutex_unlock(&ctx->mutex);

    BtEvent finished_event = {};
    finished_event.type = BT_EVENT_SCAN_FINISHED;
    fireEvent(ctx, finished_event);

    return ERROR_NONE;
}

error_t apiScanStop(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;
    mutex_lock(&ctx->mutex);
    ctx->scanning = false;
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

bool apiIsScanning(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return false;
    mutex_lock(&ctx->mutex);
    const bool scanning = ctx->scanning;
    mutex_unlock(&ctx->mutex);
    return scanning;
}

bool apiIsPeripheralActive(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return false;
    // The mock has no peripheral profiles, so nothing is ever serving.
    (void)ctx;
    return false;
}

error_t apiPair(Device* device, const BtAddr addr) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || addr == nullptr) return ERROR_INVALID_ARGUMENT;

    const size_t index = findPeer(addr);
    mutex_lock(&ctx->mutex);
    if (ctx->radioState != BT_RADIO_STATE_ON) {
        mutex_unlock(&ctx->mutex);
        return ERROR_INVALID_STATE;
    }
    if (index == MOCK_PEER_COUNT) {
        mutex_unlock(&ctx->mutex);
        return ERROR_NOT_FOUND;
    }
    ctx->peers[index].paired = true;
    mutex_unlock(&ctx->mutex);

    BtEvent event = {};
    event.type = BT_EVENT_PAIR_RESULT;
    memcpy(event.pair_result.addr, addr, BT_ADDR_LEN);
    event.pair_result.result = BT_PAIR_RESULT_SUCCESS;
    event.pair_result.profile = BT_PROFILE_HID_HOST;
    fireEvent(ctx, event);
    return ERROR_NONE;
}

error_t apiUnpair(Device* device, const BtAddr addr) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || addr == nullptr) return ERROR_INVALID_ARGUMENT;

    const size_t index = findPeer(addr);
    if (index == MOCK_PEER_COUNT) return ERROR_NOT_FOUND;

    mutex_lock(&ctx->mutex);
    const bool was_connected = ctx->peers[index].connectedProfiles != 0;
    ctx->peers[index] = {};
    mutex_unlock(&ctx->mutex);

    if (was_connected) fireConnectState(ctx, index);
    return ERROR_NONE;
}

error_t apiGetPairedPeers(Device* device, BtPeerRecord* out, size_t* count) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || count == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->mutex);
    size_t paired_count = 0;
    for (size_t i = 0; i < MOCK_PEER_COUNT; i++) {
        if (!ctx->peers[i].paired) continue;
        if (out != nullptr && paired_count < *count) {
            fillPeerRecord(ctx, i, &out[paired_count]);
        }
        paired_count++;
    }
    mutex_unlock(&ctx->mutex);

    // With a buffer, the count is the number of records written
    *count = out != nullptr && paired_count > *count ? *count : paired_count;
    return ERROR_NONE;
}

error_t apiHostConnect(Device* device, const BtAddr addr) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || addr == nullptr) return ERROR_INVALID_ARGUMENT;

    const size_t index = findPeer(addr);
    mutex_lock(&ctx->mutex);
    if (ctx->radioState != BT_RADIO_STATE_ON) {
        mutex_unlock(&ctx->mutex);
        return ERROR_INVALID_STATE;
    }
    if (index == MOCK_PEER_COUNT) {
        mutex_unlock(&ctx->mutex);
        return ERROR_NOT_FOUND;
    }
    ctx->peers[index].paired = true;
    ctx->peers[index].connectedProfiles |= 1 << BT_PROFILE_HID_HOST;
    mutex_unlock(&ctx->mutex);

    // No real peer to negotiate with, so the mock always succeeds instantly.
    fireProfileState(ctx, index, BT_PROFILE_HID_HOST, BT_PROFILE_STATE_CONNECTING);
    fireProfileState(ctx, index, BT_PROFILE_HID_HOST, BT_PROFILE_STATE_CONNECTED);
    fireConnectState(ctx, index);
    return ERROR_NONE;
}

error_t apiHostDisconnect(Device* device, const BtAddr addr) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || addr == nullptr) return ERROR_INVALID_ARGUMENT;

    const size_t index = findPeer(addr);
    if (index == MOCK_PEER_COUNT) return ERROR_NOT_FOUND;

    mutex_lock(&ctx->mutex);
    const bool was_connected = (ctx->peers[index].connectedProfiles & (1 << BT_PROFILE_HID_HOST)) != 0;
    ctx->peers[index].connectedProfiles &= ~(1 << BT_PROFILE_HID_HOST);
    mutex_unlock(&ctx->mutex);
    if (!was_connected) return ERROR_NONE;

    fireProfileState(ctx, index, BT_PROFILE_HID_HOST, BT_PROFILE_STATE_DISCONNECTING);
    fireProfileState(ctx, index, BT_PROFILE_HID_HOST, BT_PROFILE_STATE_IDLE);
    fireConnectState(ctx, index);
    return ERROR_NONE;
}

error_t apiEventSubscribe(Device* device, BtEventSubscription* sub, TaskEventGroup* event_group) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || sub == nullptr || event_group == nullptr) return ERROR_INVALID_ARGUMENT;

    uint32_t bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    mutex_lock(&ctx->subscriptionsMutex);

    // Avoid cyclic subscription list that would loop forever
    for (BtEventSubscription* existing = ctx->subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&ctx->subscriptionsMutex);
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }

    sub->internal.event_group = event_group;
    sub->bit = bit;
    sub->internal.next = ctx->subscriptions;
    ctx->subscriptions = sub;
    mutex_unlock(&ctx->subscriptionsMutex);
    return ERROR_NONE;
}

error_t apiEventUnsubscribe(Device* device, BtEventSubscription* sub) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || sub == nullptr) return ERROR_INVALID_ARGUMENT;

    error_t result = ERROR_NOT_FOUND;
    mutex_lock(&ctx->subscriptionsMutex);
    for (BtEventSubscription** link = &ctx->subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&ctx->subscriptionsMutex);

    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }
    return result;
}

error_t apiSetDeviceName(Device* device, const char* name) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || name == nullptr || strlen(name) > BLE_DEVICE_NAME_MAX) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    strncpy(ctx->deviceName, name, BLE_DEVICE_NAME_MAX);
    ctx->deviceName[BLE_DEVICE_NAME_MAX] = '\0';
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t apiGetDeviceName(Device* device, char* buf, size_t buf_len) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || buf == nullptr || buf_len == 0) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    strncpy(buf, ctx->deviceName, buf_len - 1);
    buf[buf_len - 1] = '\0';
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t apiSetIdentity(Device* device, const char* identity) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_ARGUMENT;
    if (identity != nullptr && strlen(identity) > BLE_IDENTITY_MAX) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    if (identity == nullptr || identity[0] == '\0') {
        ctx->identity[0] = '\0';
    } else {
        strncpy(ctx->identity, identity, BLE_IDENTITY_MAX);
        ctx->identity[BLE_IDENTITY_MAX] = '\0';
    }
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t apiGetIdentity(Device* device, char* buf, size_t buf_len) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || buf == nullptr || buf_len == 0) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    const char* identity = (ctx->identity[0] != '\0') ? ctx->identity : ctx->deviceName;
    strncpy(buf, identity, buf_len - 1);
    buf[buf_len - 1] = '\0';
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

const BluetoothApi mock_bluetooth_api = {
    .get_radio_state = apiGetRadioState,
    .set_radio_enabled = apiSetRadioEnabled,
    .scan_start = apiScanStart,
    .scan_stop = apiScanStop,
    .is_scanning = apiIsScanning,
    .is_peripheral_active = apiIsPeripheralActive,
    .pair = apiPair,
    .unpair = apiUnpair,
    .get_paired_peers = apiGetPairedPeers,
    .host_connect = apiHostConnect,
    .host_disconnect = apiHostDisconnect,
    .event_subscribe = apiEventSubscribe,
    .event_unsubscribe = apiEventUnsubscribe,
    .set_device_name = apiSetDeviceName,
    .get_device_name = apiGetDeviceName,
    .set_identity = apiSetIdentity,
    .get_identity = apiGetIdentity,
};

// ---- Driver lifecycle ----
// startDevice()/stopDevice() only allocate/free this driver's bookkeeping (subscriber list
// included), so event subscribers can stay subscribed across radio on/off toggles.

error_t startDevice(Device* device) {
    auto* ctx = new(std::nothrow) MockBluetoothCtx();
    if (ctx == nullptr) return ERROR_OUT_OF_MEMORY;

    ctx->device = device;
    strncpy(ctx->deviceName, MOCK_DEFAULT_NAME, BLE_DEVICE_NAME_MAX);
    mutex_construct(&ctx->mutex);
    mutex_construct(&ctx->subscriptionsMutex);

    device_set_driver_data(device, ctx);
    return ERROR_NONE;
}

error_t stopDevice(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_NONE;

    if (ctx->radioState == BT_RADIO_STATE_ON) {
        apiSetRadioEnabled(device, false);
    }

    // Release any subscribers that never unsubscribed: device_stop() doesn't wait for apps still
    // using this device, so a later bluetooth_event_unsubscribe() would find no ctx and skip
    // releasing the bit.
    mutex_lock(&ctx->subscriptionsMutex);
    for (BtEventSubscription* sub = ctx->subscriptions; sub != nullptr;) {
        BtEventSubscription* next = sub->internal.next;
        // Force-close, don't destruct: a concurrent bluetooth_event_poll() may hold/await this
        // same lock (see BtEventSubscription::internal::closed).
        mutex_lock(&sub->internal.ring_mutex);
        sub->internal.closed = true;
        mutex_unlock(&sub->internal.ring_mutex);
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
        sub = next;
    }
    ctx->subscriptions = nullptr;
    mutex_unlock(&ctx->subscriptionsMutex);

    device_set_driver_data(device, nullptr);
    mutex_destruct(&ctx->subscriptionsMutex);
    mutex_destruct(&ctx->mutex);
    delete ctx;

    return ERROR_NONE;
}

} // namespace

extern "C" {

Driver mock_bluetooth_driver = {
    .name = "mock_bluetooth",
    .compatible = (const char*[]) { "mock,bluetooth", nullptr },
    .start_device = startDevice,
    .stop_device = stopDevice,
    .probe = nullptr,
    .api = (const void*)&mock_bluetooth_api,
    .device_type = &BLUETOOTH_TYPE,
    .internal = nullptr
};

} // extern "C"
