#include <tactility/drivers/usb_host_hid.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define USB_HID_API(driver) ((struct UsbHidApi*)(driver)->api)

extern "C" {

const struct DeviceType USB_HOST_HID_TYPE = {
    .name = "usb-host-hid",
};

bool usb_host_hid_is_connected(struct Device* device) {
    auto* api = USB_HID_API(device_get_driver(device));
    return api->is_connected(device);
}

bool usb_host_hid_subscribe(struct Device* device, UsbHidQueueHandle event_queue) {
    auto* api = USB_HID_API(device_get_driver(device));
    return api->subscribe(device, event_queue);
}

void usb_host_hid_unsubscribe(struct Device* device, UsbHidQueueHandle event_queue) {
    auto* api = USB_HID_API(device_get_driver(device));
    api->unsubscribe(device, event_queue);
}

} // extern "C"
