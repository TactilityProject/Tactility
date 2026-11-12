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

bool bluetooth_is_radio_on_or_pending(struct Device* device) {
    if (!device_is_ready(device)) {
        return false;
    }
    enum BtRadioState state;
    if (BT_API(device)->get_radio_state(device, &state) != ERROR_NONE) {
        return false;
    }
    return state == BT_RADIO_STATE_ON || state == BT_RADIO_STATE_ON_PENDING;
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

bool bluetooth_is_peripheral_active(struct Device* device) {
    return BT_API(device)->is_peripheral_active(device);
}

// ---- Pairing ----

error_t bluetooth_pair(struct Device* device, const BtAddr addr) {
    return BT_API(device)->pair(device, addr);
}

error_t bluetooth_unpair(struct Device* device, const BtAddr addr) {
    return BT_API(device)->unpair(device, addr);
}

error_t bluetooth_get_paired_peers(struct Device* device, struct BtPeerRecord* out, size_t* count) {
    return BT_API(device)->get_paired_peers(device, out, count);
}

// ---- Connect / disconnect ----

error_t bluetooth_host_connect(struct Device* device, const BtAddr addr) {
    return BT_API(device)->host_connect(device, addr);
}

error_t bluetooth_host_disconnect(struct Device* device, const BtAddr addr) {
    return BT_API(device)->host_disconnect(device, addr);
}

// ---- Event subscription ----

error_t bluetooth_event_subscribe(struct Device* device, struct BtEventSubscription* sub, struct TaskEventGroup* event_group) {
    mutex_construct(&sub->internal.ring_mutex);
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.closed = false;
    sub->internal.constructed = true;

    error_t result = BT_API(device)->event_subscribe(device, sub, event_group);
    if (result != ERROR_NONE) {
        sub->internal.constructed = false;
        mutex_destruct(&sub->internal.ring_mutex);
    }
    return result;
}

error_t bluetooth_event_unsubscribe(struct Device* device, struct BtEventSubscription* sub) {
    error_t result = BT_API(device)->event_unsubscribe(device, sub);
    // sub was never subscribed, or was already unsubscribed. ring_mutex was never constructed,
    // unsafe to lock or destruct.
    if (!sub->internal.constructed) {
        return result;
    }
    mutex_lock(&sub->internal.ring_mutex);
    bool was_closed = sub->internal.closed;
    mutex_unlock(&sub->internal.ring_mutex);
    // Destruct on success or force-close.
    if (result == ERROR_NONE || was_closed) {
        sub->internal.constructed = false;
        mutex_destruct(&sub->internal.ring_mutex);
    }
    return result;
}

error_t bluetooth_event_poll(struct BtEventSubscription* sub, struct BtEvent* out_event) {
    mutex_lock(&sub->internal.ring_mutex);
    if (sub->internal.closed) {
        mutex_unlock(&sub->internal.ring_mutex);
        return ERROR_TIMEOUT;
    }
    bool has_event = sub->internal.count > 0;
    if (has_event) {
        *out_event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % BT_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    mutex_unlock(&sub->internal.ring_mutex);
    return has_event ? ERROR_NONE : ERROR_TIMEOUT;
}

error_t bluetooth_set_device_name(struct Device* device, const char* name) {
    return BT_API(device)->set_device_name(device, name);
}

error_t bluetooth_get_device_name(struct Device* device, char* buf, size_t buf_len) {
    return BT_API(device)->get_device_name(device, buf, buf_len);
}

error_t bluetooth_set_identity(struct Device* device, const char* identity) {
    return BT_API(device)->set_identity(device, identity);
}

error_t bluetooth_get_identity(struct Device* device, char* buf, size_t buf_len) {
    return BT_API(device)->get_identity(device, buf, buf_len);
}

// ---- Device type ----

const struct DeviceType BLUETOOTH_TYPE = {
    .name = "bluetooth",
};

} // extern "C"
