// SPDX-License-Identifier: Apache-2.0

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/posix_i2c.h>
#include <tactility/log.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <unistd.h>

#ifdef __linux__
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#endif

#define TAG "posix_i2c"

namespace {

struct PosixI2cInternal {
    Mutex mutex {};
    int fd = -1;

    PosixI2cInternal() {
        mutex_construct(&mutex);
    }

    ~PosixI2cInternal() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((const PosixI2cConfig*)device->config)
#define GET_DATA(device) ((PosixI2cInternal*)device_get_driver_data(device))

#define lock(data) mutex_lock(&data->mutex)
#define unlock(data) mutex_unlock(&data->mutex)

#ifdef __linux__

// The kernel adapter applies its own timeout, so the TickType_t timeouts of the API are not used.
error_t transfer(Device* device, i2c_msg* messages, uint32_t message_count) {
    auto* data = GET_DATA(device);
    i2c_rdwr_ioctl_data request = { .msgs = messages, .nmsgs = message_count };
    lock(data);
    int result = ioctl(data->fd, I2C_RDWR, &request);
    int error_number = errno;
    unlock(data);
    if (result >= 0) return ERROR_NONE;
    if (error_number == ETIMEDOUT) return ERROR_TIMEOUT;
    if (error_number == ENXIO || error_number == EREMOTEIO) return ERROR_NOT_FOUND;
    return ERROR_RESOURCE;
}

i2c_msg write_message(uint8_t address, const uint8_t* data, uint16_t size) {
    return { .addr = address, .flags = 0, .len = size, .buf = const_cast<uint8_t*>(data) };
}

i2c_msg read_message(uint8_t address, uint8_t* data, uint16_t size) {
    return { .addr = address, .flags = I2C_M_RD, .len = size, .buf = data };
}

// ---- I2cControllerApi ----

error_t read(Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout) {
    if (dataSize > UINT16_MAX) return ERROR_INVALID_ARGUMENT;
    i2c_msg messages[] = { read_message(address, data, dataSize) };
    return transfer(device, messages, 1);
}

error_t write(Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    i2c_msg messages[] = { write_message(address, data, dataSize) };
    return transfer(device, messages, 1);
}

error_t write_read(Device* device, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout) {
    if (writeDataSize > UINT16_MAX || readDataSize > UINT16_MAX) return ERROR_INVALID_ARGUMENT;
    i2c_msg messages[] = {
        write_message(address, writeData, writeDataSize),
        read_message(address, readData, readDataSize)
    };
    return transfer(device, messages, 2);
}

error_t read_register(Device* device, uint8_t address, uint8_t reg, uint8_t* data, size_t dataSize, TickType_t timeout) {
    return write_read(device, address, &reg, 1, data, dataSize, timeout);
}

error_t write_register(Device* device, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    if (dataSize == UINT16_MAX) return ERROR_INVALID_ARGUMENT;
    auto* buffer = new(std::nothrow) uint8_t[dataSize + 1];
    if (buffer == nullptr) return ERROR_OUT_OF_MEMORY;
    buffer[0] = reg;
    memcpy(buffer + 1, data, dataSize);
    error_t error = write(device, address, buffer, dataSize + 1, timeout);
    delete[] buffer;
    return error;
}

error_t probe(Device* device, uint8_t address, TickType_t timeout) {
    uint8_t byte;
    return read(device, address, &byte, 1, timeout);
}

#endif

const I2cControllerApi posix_i2c_api = {
#ifdef __linux__
    .read = read,
    .write = write,
    .write_read = write_read,
    .read_register = read_register,
    .write_register = write_register,
    .probe = probe
#endif
};

// ---- Driver lifecycle ----

error_t start_device(Device* device) {
#ifdef __linux__
    const char* path = GET_CONFIG(device)->path;
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LOG_E(TAG, "%s failed to open %s: %s", device->name, path, strerror(errno));
        return ERROR_RESOURCE;
    }

    auto* data = new(std::nothrow) PosixI2cInternal();
    if (data == nullptr) {
        close(fd);
        return ERROR_OUT_OF_MEMORY;
    }
    data->fd = fd;
    device_set_driver_data(device, data);
    return ERROR_NONE;
#else
    LOG_E(TAG, "%s: I2C requires Linux i2c-dev", device->name);
    return ERROR_NOT_SUPPORTED;
#endif
}

error_t stop_device(Device* device) {
    auto* data = GET_DATA(device);
    device_set_driver_data(device, nullptr);
    close(data->fd);
    delete data;
    return ERROR_NONE;
}

} // namespace

extern "C" {

Driver posix_i2c_driver = {
    .name = "posix_i2c",
    .compatible = (const char*[]) { "posix,i2c", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .probe = nullptr,
    .api = (const void*)&posix_i2c_api,
    .device_type = &I2C_CONTROLLER_TYPE,
    .internal = nullptr
};

} // extern "C"
