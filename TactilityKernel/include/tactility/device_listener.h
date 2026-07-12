#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

enum DeviceEvent {
    DEVICE_EVENT_STARTED,
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

void device_listener_add(DeviceListenerCallback* callback, void* context);

void device_listener_remove(DeviceListenerCallback* callback);

#ifdef __cplusplus
}
#endif
