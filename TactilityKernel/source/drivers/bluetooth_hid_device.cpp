#include <tactility/drivers/bluetooth_hid_device.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define BT_HID_DEVICE_API(device) ((const struct BtHidDeviceApi*)device_get_driver(device)->api)

extern "C" {

const struct DeviceType BLUETOOTH_HID_DEVICE_TYPE = {
    .name = "bluetooth-hid-device",
};

struct Device* bluetooth_hid_device_get_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&BLUETOOTH_HID_DEVICE_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        if (device_is_ready(dev)) {
            *static_cast<struct Device**>(ctx) = dev;
            return false;
        }
        return true;
    });
    return found;
}

error_t bluetooth_hid_device_start(struct Device* device, enum BtHidDeviceMode mode) {
    return BT_HID_DEVICE_API(device)->start(device, mode);
}

error_t bluetooth_hid_device_stop(struct Device* device) {
    return BT_HID_DEVICE_API(device)->stop(device);
}

error_t bluetooth_hid_device_send_key(struct Device* device, uint8_t keycode, bool pressed) {
    return BT_HID_DEVICE_API(device)->send_key(device, keycode, pressed);
}

error_t bluetooth_hid_device_send_keyboard(struct Device* device, const uint8_t* report, size_t len) {
    return BT_HID_DEVICE_API(device)->send_keyboard(device, report, len);
}

error_t bluetooth_hid_device_send_consumer(struct Device* device, const uint8_t* report, size_t len) {
    return BT_HID_DEVICE_API(device)->send_consumer(device, report, len);
}

error_t bluetooth_hid_device_send_mouse(struct Device* device, const uint8_t* report, size_t len) {
    return BT_HID_DEVICE_API(device)->send_mouse(device, report, len);
}

error_t bluetooth_hid_device_send_gamepad(struct Device* device, const uint8_t* report, size_t len) {
    return BT_HID_DEVICE_API(device)->send_gamepad(device, report, len);
}

bool bluetooth_hid_device_is_connected(struct Device* device) {
    return BT_HID_DEVICE_API(device)->is_connected(device);
}

} // extern "C"
