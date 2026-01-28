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

static const auto LOGGER = Logger("I2C");

struct Data {
    Mutex mutex;
    bool isConfigured = false;
    Device* device = nullptr;
#ifdef ESP_PLATFORM
    Esp32I2cConfig config = {
        .port = I2C_NUM_0,
        .clockFrequency = 0,
        .pinSda = 0,
        .pinScl = 0,
        .pinSdaPullUp = false,
        .pinSclPullUp = false
    };
#endif
};

static Data dataArray[I2C_NUM_MAX];

#ifdef ESP_PLATFORM
void registerDriver(Configuration configuration) {
    Data& data = dataArray[configuration.port];

    // Should only be called on init
    check(data.device == nullptr);

    data.config.port = configuration.port;
    data.config.clockFrequency = configuration.config.master.clk_speed;
    data.config.pinSda = configuration.config.sda_io_num;
    data.config.pinScl = configuration.config.scl_io_num;
    data.config.pinSdaPullUp = configuration.config.sda_pullup_en;
    data.config.pinSclPullUp = configuration.config.scl_pullup_en;

    data.device = new Device();
    data.device->name = configuration.name.c_str();
    data.device->config = &data.config;
    data.device->parent = nullptr;

    device_construct_add(data.device, "espressif,esp32-i2c");
}

bool initExistingKernelDevice(const Configuration& configuration) {
    Data& data = dataArray[configuration.port];

    // Should only be called on init
    check(data.device == nullptr);

    struct Params {
        i2c_port_t port;
        Device* device;
    };

    Params params = {
        .port = configuration.port,
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

    if (params.device == nullptr) {
        return false;
    }

    data.device = params.device;
    memcpy(&data.config, params.device->config, sizeof(Esp32I2cConfig));
    return true;
}
#endif

bool init(const std::vector<Configuration>& configurations) {
   LOGGER.info("Init");
#ifdef ESP_PLATFORM
   for (const auto& configuration: configurations) {
       if (configuration.config.mode != I2C_MODE_MASTER) {
           LOGGER.error("Currently only master mode is supported");
           return false;
       }
       if (initExistingKernelDevice(configuration)) {
           LOGGER.info("Initialized port {} with existing kernel device", (int)configuration.port);
       } else {
           registerDriver(configuration);
       }
       dataArray[configuration.port].isConfigured = true;
   }

   for (const auto& config: configurations) {
       if (config.initMode == InitMode::ByTactility) {
           if (!start(config.port)) {
               return false;
           }
       }
   }
#endif
   return true;
}

bool start(i2c_port_t port) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();

    Data& data = dataArray[port];

    if (!data.isConfigured) {
        LOGGER.error("({}) Starting: Not configured", static_cast<int>(port));
        return false;
    }

    check(data.device);

    error_t error = device_start(data.device);
    if (error != ERROR_NONE) {
        LOGGER.error("Failed to start device {}: {}", data.device->name, error_to_string(error));
    }

    return true;
#else
    return false;
#endif
}

bool stop(i2c_port_t port) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();

    Data& data = dataArray[port];
    if (!dataArray[port].isConfigured) return false;
    return device_stop(data.device) == ERROR_NONE;
#else
    return false;
#endif
}

bool isStarted(i2c_port_t port) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();
    if (!dataArray[port].isConfigured) return false;
    return device_is_ready(dataArray[port].device);
#else
    return false;
#endif
}

bool masterRead(i2c_port_t port, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();
    if (!dataArray[port].isConfigured) return false;
    return i2c_controller_read(dataArray[port].device, address, data, dataSize, timeout) == ERROR_NONE;
#else
    return false;
#endif
}

bool masterReadRegister(i2c_port_t port, uint8_t address, uint8_t reg, uint8_t* data, size_t dataSize, TickType_t timeout) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();
    if (!dataArray[port].isConfigured) return false;
    return i2c_controller_read_register(dataArray[port].device, address, reg, data, dataSize, timeout) == ERROR_NONE;
#else
    return false;
#endif
}

bool masterWrite(i2c_port_t port, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();
    if (!dataArray[port].isConfigured) return false;
    return i2c_controller_write(dataArray[port].device, address, data, dataSize, timeout) == ERROR_NONE;
#else
    return false;
#endif
}

bool masterWriteRegister(i2c_port_t port, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();
    if (!dataArray[port].isConfigured) return false;
    return i2c_controller_write_register(dataArray[port].device, address, reg, data, dataSize, timeout) == ERROR_NONE;
#else
    return false;
#endif
}

bool masterWriteRegisterArray(i2c_port_t port, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();
    if (!dataArray[port].isConfigured) return false;
    return i2c_controller_write_register_array(dataArray[port].device, address, data, dataSize, timeout) == ERROR_NONE;
#else
    return false;
#endif
}

bool masterWriteRead(i2c_port_t port, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();
    if (!dataArray[port].isConfigured) return false;
    return i2c_controller_write_read(dataArray[port].device, address, writeData, writeDataSize, readData, readDataSize, timeout) == ERROR_NONE;
#else
    return false;
#endif
}

bool masterHasDeviceAtAddress(i2c_port_t port, uint8_t address, TickType_t timeout) {
#ifdef ESP_PLATFORM
    auto lock = getLock(port).asScopedLock();
    lock.lock();
    if (!dataArray[port].isConfigured) return false;
    return i2c_controller_has_device_at_address(dataArray[port].device, address, timeout) == ERROR_NONE;
#else
    return false;
#endif
}

Lock& getLock(i2c_port_t port) {
    return dataArray[port].mutex;
}

} // namespace
