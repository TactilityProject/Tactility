#include <tactility/drivers/usb_host_msc.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define USB_MSC_API(driver) ((struct UsbMscApi*)(driver)->api)

extern "C" {

const struct DeviceType USB_HOST_MSC_TYPE = {
    .name = "usb-host-msc",
};

bool usb_msc_eject(struct Device* device, const char* mount_path) {
    auto* api = USB_MSC_API(device_get_driver(device));
    return api->eject && api->eject(device, mount_path);
}

} // extern "C"
