#include <Tactility/Mutex.h>

#include <tactility/check.h>
#include <tactility/drivers/i2c_controller.h>

#ifdef ESP_PLATFORM
#include <tactility/drivers/esp32_i2c.h>
#endif

namespace tt::hal::i2c {

Device* findDevice(i2c_port_t port) {
#ifdef ESP_PLATFORM
    struct Params {
        i2c_port_t port;
        Device* device;
    };

    Params params = {
        .port = port,
        .device = nullptr
    };

    device_for_each_of_type(&I2C_CONTROLLER_TYPE, &params, [](auto* device, auto* context) {
        auto* params_ptr = (Params*)context;
        auto* driver = device_get_driver(device);
        if (driver == nullptr) return true;
        if (!driver_is_compatible(driver, "espressif,esp32-i2c")) return true;
        const auto* config = static_cast<const Esp32I2cConfig*>(device->config);
        if (config->port != params_ptr->port) return true;
        // Found it, stop iterating
        params_ptr->device = device;
        return false;
    });

    return params.device;
#else
    return nullptr;
#endif
}

bool masterHasDeviceAtAddress(i2c_port_t port, uint8_t address, TickType_t timeout) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return i2c_controller_has_device_at_address(device, address, timeout) == ERROR_NONE;
}

} // namespace
