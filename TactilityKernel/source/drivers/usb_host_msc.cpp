#include <tactility/drivers/usb_host_msc.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define USB_MSC_API(driver) ((struct UsbMscApi*)(driver)->api)

extern "C" {

const struct DeviceType USB_HOST_MSC_TYPE = {
    .name = "usb-host-msc",
};

bool usb_msc_eject(const char* mount_path) {
    if (!mount_path) return false;
    struct Device* device = device_find_first_active_by_type(&USB_HOST_MSC_TYPE);
    if (!device) return false;
    struct Driver* drv = device_get_driver(device);
    if (!drv || !drv->api) return false;
    auto* api = USB_MSC_API(drv);
    return api->eject && api->eject(device, mount_path);
}

} // extern "C"
