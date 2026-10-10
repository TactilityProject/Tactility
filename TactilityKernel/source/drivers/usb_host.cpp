#include <tactility/drivers/usb_host.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define USB_HOST_API(driver) ((struct UsbHostApi*)(driver)->api)

extern "C" {

const struct DeviceType USB_HOST_TYPE = {
    .name = "usb-host",
};

error_t usb_host_is_class_supported(struct Device* host, enum UsbClass usb_class, bool* supported) {
    return USB_HOST_API(device_get_driver(host))->is_class_supported(host, usb_class, supported);
}

error_t usb_host_set_class_enabled(struct Device* host, enum UsbClass usb_class, bool enabled) {
    return USB_HOST_API(device_get_driver(host))->set_class_enabled(host, usb_class, enabled);
}

error_t usb_host_is_class_enabled(struct Device* host, enum UsbClass usb_class, bool* enabled) {
    return USB_HOST_API(device_get_driver(host))->is_class_enabled(host, usb_class, enabled);
}

} // extern "C"
