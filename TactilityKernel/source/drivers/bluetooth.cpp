#include <tactility/drivers/bluetooth.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define BT_API(device) ((const struct BluetoothApi*)device_get_driver(device)->api)

extern "C" {

// ---- Device lookup ----

struct Device* bluetooth_find_first_ready_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&BLUETOOTH_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        if (device_is_ready(dev)) {
            *static_cast<struct Device**>(ctx) = dev;
            return false;
        }
        return true;
    });
    return found;
}

// ---- Core radio / scan ----

error_t bluetooth_get_radio_state(struct Device* device, enum BtRadioState* state) {
    return BT_API(device)->get_radio_state(device, state);
}

error_t bluetooth_set_radio_enabled(struct Device* device, bool enabled) {
    return BT_API(device)->set_radio_enabled(device, enabled);
}

error_t bluetooth_scan_start(struct Device* device) {
    return BT_API(device)->scan_start(device);
}

error_t bluetooth_scan_stop(struct Device* device) {
    return BT_API(device)->scan_stop(device);
}

bool bluetooth_is_scanning(struct Device* device) {
    return BT_API(device)->is_scanning(device);
}

// ---- Pairing ----

error_t bluetooth_pair(struct Device* device, const BtAddr addr) {
    return BT_API(device)->pair(device, addr);
}

error_t bluetooth_unpair(struct Device* device, const BtAddr addr) {
    return BT_API(device)->unpair(device, addr);
}

// ---- Connect / disconnect ----

error_t bluetooth_connect(struct Device* device, const BtAddr addr, enum BtProfileId profile) {
    return BT_API(device)->connect(device, addr, profile);
}

error_t bluetooth_disconnect(struct Device* device, const BtAddr addr, enum BtProfileId profile) {
    return BT_API(device)->disconnect(device, addr, profile);
}

// ---- Event callbacks ----

error_t bluetooth_add_event_callback(struct Device* device, void* context, BtEventCallback callback) {
    return BT_API(device)->add_event_callback(device, context, callback);
}

error_t bluetooth_remove_event_callback(struct Device* device, BtEventCallback callback) {
    return BT_API(device)->remove_event_callback(device, callback);
}

error_t bluetooth_set_device_name(struct Device* device, const char* name) {
    return BT_API(device)->set_device_name(device, name);
}

error_t bluetooth_get_device_name(struct Device* device, char* buf, size_t buf_len) {
    return BT_API(device)->get_device_name(device, buf, buf_len);
}

// ---- HID host active flag ----

void bluetooth_set_hid_host_active(struct Device* device, bool active) {
    BT_API(device)->set_hid_host_active(device, active);
}

void bluetooth_fire_event(struct Device* device, struct BtEvent event) {
    BT_API(device)->fire_event(device, event);
}

// ---- Device type ----

const struct DeviceType BLUETOOTH_TYPE = {
    .name = "bluetooth",
};

} // extern "C"
