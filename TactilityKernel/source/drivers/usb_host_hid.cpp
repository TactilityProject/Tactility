#include <tactility/drivers/usb_host_hid.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define USB_HID_API(driver) ((struct UsbHidApi*)(driver)->api)

extern "C" {

const struct DeviceType USB_HOST_HID_TYPE = {
    .name = "usb-host-hid",
};

static const struct UsbHidApi* get_active_hid_api(struct Device** out_device) {
    struct Device* device = device_find_first_active_by_type(&USB_HOST_HID_TYPE);
    if (!device) return nullptr;
    struct Driver* drv = device_get_driver(device);
    if (!drv || !drv->api) return nullptr;
    if (out_device) *out_device = device;
    return USB_HID_API(drv);
}

bool usb_host_hid_is_connected(void) {
    struct Device* device = nullptr;
    const struct UsbHidApi* api = get_active_hid_api(&device);
    return api && api->is_connected && api->is_connected(device);
}

bool usb_host_hid_subscribe(UsbHidQueueHandle event_queue) {
    struct Device* device = nullptr;
    const struct UsbHidApi* api = get_active_hid_api(&device);
    return api && api->subscribe && api->subscribe(device, event_queue);
}

void usb_host_hid_unsubscribe(UsbHidQueueHandle event_queue) {
    struct Device* device = nullptr;
    const struct UsbHidApi* api = get_active_hid_api(&device);
    if (api && api->unsubscribe) api->unsubscribe(device, event_queue);
}

} // extern "C"
