#include <tactility/drivers/bluetooth_serial.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define BT_SERIAL_API(device) ((const struct BtSerialApi*)device_get_driver(device)->api)

extern "C" {

const struct DeviceType BLUETOOTH_SERIAL_TYPE = {
    .name = "bluetooth-serial",
};

struct Device* bluetooth_serial_get_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&BLUETOOTH_SERIAL_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        if (device_is_ready(dev)) {
            *static_cast<struct Device**>(ctx) = dev;
            return false;
        }
        return true;
    });
    return found;
}

error_t bluetooth_serial_start(struct Device* device) {
    return BT_SERIAL_API(device)->start(device);
}

error_t bluetooth_serial_stop(struct Device* device) {
    return BT_SERIAL_API(device)->stop(device);
}

error_t bluetooth_serial_write(struct Device* device, const uint8_t* data, size_t len, size_t* written) {
    return BT_SERIAL_API(device)->write(device, data, len, written);
}

error_t bluetooth_serial_read(struct Device* device, uint8_t* data, size_t max_len, size_t* read_out) {
    return BT_SERIAL_API(device)->read(device, data, max_len, read_out);
}

bool bluetooth_serial_is_connected(struct Device* device) {
    return BT_SERIAL_API(device)->is_connected(device);
}

} // extern "C"
