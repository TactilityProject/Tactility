#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

enum DeviceEvent {
    DEVICE_EVENT_STARTED,
    DEVICE_EVENT_STOPPING,
    DEVICE_EVENT_STOPPED,
};

typedef void (*DeviceListenerCallback)(
    struct Device *dev,
    enum DeviceEvent event,
    void* context
);

struct DeviceEventListener {
    DeviceListenerCallback callback;
    void* callback_context;
};

void device_listener_add(DeviceListenerCallback callback, void* context);

// Removes the (callback, context) pair added via device_listener_add(). Matches on context too,
// so removing one instance's listener doesn't remove another instance's that shares the callback.
void device_listener_remove(DeviceListenerCallback callback, void* context);

#ifdef __cplusplus
}
#endif
