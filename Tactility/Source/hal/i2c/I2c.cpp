#include <Tactility/hal/i2c/I2c.h>

#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>

#include <tactility/check.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/time.h>

#ifdef ESP_PLATFORM
#include <tactility/drivers/esp32_i2c.h>
#endif

namespace tt::hal::i2c {

class NoLock final : public tt::Lock {
    bool lock(TickType_t timeout) const override { return true; }
    void unlock() const override { /* NO-OP */ }
};

static NoLock NO_LOCK;

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

    for_each_device_of_type(&I2C_CONTROLLER_TYPE, &params, [](auto* device, auto* context) {
        auto* params_ptr = (Params*)context;
        auto* driver = device_get_driver(device);
        if (driver == nullptr) return true;
        if (!driver_is_compatible(driver, "espressif,esp32-i2c")) return true;
        i2c_port_t port;
        if (esp32_i2c_get_port(device, &port) != ERROR_NONE) return true;
        if (port != params_ptr->port) return true;
        // Found it, stop iterating
        params_ptr->device = device;
        return false;
    });

    return params.device;
#else
    return nullptr;
#endif
}

bool isStarted(i2c_port_t port) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return device_is_ready(device);
}

const char* getName(i2c_port_t port) {
    auto* device = findDevice(port);
    if (device == nullptr) return nullptr;
    return device->name;
}

bool masterRead(i2c_port_t port, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return i2c_controller_read(device, address, data, dataSize, timeout) == ERROR_NONE;
}

bool masterReadRegister(i2c_port_t port, uint8_t address, uint8_t reg, uint8_t* data, size_t dataSize, TickType_t timeout) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return i2c_controller_read_register(device, address, reg, data, dataSize, timeout) == ERROR_NONE;
}

bool masterWrite(i2c_port_t port, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return i2c_controller_write(device, address, data, dataSize, timeout) == ERROR_NONE;
}

bool masterWriteRegister(i2c_port_t port, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return i2c_controller_write_register(device, address, reg, data, dataSize, timeout) == ERROR_NONE;
}

bool masterWriteRegisterArray(i2c_port_t port, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return i2c_controller_write_register_array(device, address, data, dataSize, timeout) == ERROR_NONE;
}

bool masterWriteRead(i2c_port_t port, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return i2c_controller_write_read(device, address, writeData, writeDataSize, readData, readDataSize, timeout) == ERROR_NONE;
}

bool masterHasDeviceAtAddress(i2c_port_t port, uint8_t address, TickType_t timeout) {
    auto* device = findDevice(port);
    if (device == nullptr) return false;
    return i2c_controller_has_device_at_address(device, address, timeout) == ERROR_NONE;
}

Lock& getLock(i2c_port_t port) {
    return NO_LOCK;
}

} // namespace
