#include <tactility/drivers/usb_host_msc.h>
#include <tactility/device.h>
#include <tactility/driver.h>

static const struct UsbMscApi* get_msc_api(struct Device* device) {
    if (!device) return nullptr;
    struct Driver* drv = device_get_driver(device);
    if (!drv || !drv->api) return nullptr;
    return static_cast<const struct UsbMscApi*>(drv->api);
}

extern "C" {

const struct DeviceType USB_HOST_MSC_TYPE = {
    .name = "usb-host-msc",
};

struct Device* usb_host_msc_get_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&USB_HOST_MSC_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        if (device_is_ready(dev)) {
            *static_cast<struct Device**>(ctx) = dev;
            return false;
        }
        return true;
    });
    return found;
}

bool usb_msc_eject(const char* mount_path) {
    if (!mount_path) return false;
    struct Device* device = usb_host_msc_get_device();
    const struct UsbMscApi* api = get_msc_api(device);
    return api && api->eject && api->eject(device, mount_path);
}

} // extern "C"
