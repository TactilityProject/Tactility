#include <tactility/drivers/usb_device_controller.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define USB_DEVICE_CONTROLLER_API(device) ((const struct UsbDeviceControllerApi*)device_get_driver(device)->api)

extern "C" {

const struct DeviceType USB_DEVICE_CONTROLLER_TYPE = {
    .name = "usb-device-controller",
};

struct Device* usb_device_controller_get() {
    struct Device* found = nullptr;
    device_get_first_active_by_type(&USB_DEVICE_CONTROLLER_TYPE, &found);
    return found;
}

error_t usb_device_controller_begin_claim(struct Device* device) {
    return USB_DEVICE_CONTROLLER_API(device)->begin_claim(device);
}

error_t usb_device_controller_allocate_interfaces(struct Device* device, uint8_t interface_count,
                                                   uint8_t in_endpoint_count, uint8_t out_endpoint_count,
                                                   struct UsbInterfaceAllocation* out_allocation) {
    return USB_DEVICE_CONTROLLER_API(device)->allocate_interfaces(device, interface_count, in_endpoint_count, out_endpoint_count, out_allocation);
}

error_t usb_device_controller_claim(struct Device* device, enum UsbDeviceClass usb_class, const struct UsbDeviceClaimConfig* config) {
    return USB_DEVICE_CONTROLLER_API(device)->claim(device, usb_class, config);
}

error_t usb_device_controller_release(struct Device* device, enum UsbDeviceClass usb_class) {
    return USB_DEVICE_CONTROLLER_API(device)->release(device, usb_class);
}

enum UsbDeviceClass usb_device_controller_get_active_class(struct Device* device) {
    return USB_DEVICE_CONTROLLER_API(device)->get_active_class(device);
}

bool usb_device_controller_is_cdc_enabled(struct Device* device) {
    return USB_DEVICE_CONTROLLER_API(device)->is_cdc_enabled(device);
}

error_t usb_device_controller_start_cdc_console(struct Device* device) {
    return USB_DEVICE_CONTROLLER_API(device)->start_cdc_console(device);
}

error_t usb_device_controller_stop_cdc_console(struct Device* device) {
    return USB_DEVICE_CONTROLLER_API(device)->stop_cdc_console(device);
}

} // extern "C"
