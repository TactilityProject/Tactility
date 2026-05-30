#include <tactility/drivers/usb_host_hid.h>
#include <tactility/device.h>
#include <tactility/driver.h>

static const struct UsbHidApi* get_hid_api(struct Device* device) {
    if (!device) return nullptr;
    struct Driver* drv = device_get_driver(device);
    if (!drv || !drv->api) return nullptr;
    return static_cast<const struct UsbHidApi*>(drv->api);
}

extern "C" {

const struct DeviceType USB_HOST_HID_TYPE = {
    .name = "usb-host-hid",
};

struct Device* usb_host_hid_get_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&USB_HOST_HID_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        if (device_is_ready(dev)) {
            *static_cast<struct Device**>(ctx) = dev;
            return false;
        }
        return true;
    });
    return found;
}

bool usb_host_hid_is_connected(void) {
    struct Device* device = usb_host_hid_get_device();
    const struct UsbHidApi* api = get_hid_api(device);
    return api && api->is_connected && api->is_connected(device);
}

bool usb_host_hid_subscribe(UsbHidQueueHandle event_queue) {
    struct Device* device = usb_host_hid_get_device();
    const struct UsbHidApi* api = get_hid_api(device);
    return api && api->subscribe && api->subscribe(device, event_queue);
}

void usb_host_hid_unsubscribe(UsbHidQueueHandle event_queue) {
    struct Device* device = usb_host_hid_get_device();
    const struct UsbHidApi* api = get_hid_api(device);
    if (api && api->unsubscribe) api->unsubscribe(device, event_queue);
}

} // extern "C"
