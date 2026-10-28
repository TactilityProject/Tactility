// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/device.h>

#define SPI_DRIVER_API(driver) ((struct SpiControllerApi*)driver->api)

extern "C" {

error_t spi_controller_lock(Device* device) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->lock(device);
}

error_t spi_controller_try_lock(Device* device, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->try_lock(device, timeout);
}

error_t spi_controller_unlock(Device* device) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->unlock(device);
}

error_t spi_controller_add_device(Device* device, const SpiDeviceConfig* config, SpiDevice** spi_device) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->add_device(device, config, spi_device);
}

error_t spi_controller_remove_device(Device* device, SpiDevice* spi_device) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->remove_device(device, spi_device);
}

error_t spi_controller_transmit(Device* device, SpiDevice* spi_device, const void* tx_data, void* rx_data, size_t length) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->transmit(device, spi_device, tx_data, rx_data, length);
}

error_t spi_controller_acquire_bus(Device* device, SpiDevice* spi_device) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->acquire_bus(device, spi_device);
}

error_t spi_controller_release_bus(Device* device, SpiDevice* spi_device) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->release_bus(device, spi_device);
}

error_t spi_controller_lock_bus_of(Device* device) {
    Device* parent = device_get_parent(device);
    if (parent == nullptr || device_get_type(parent) != &SPI_CONTROLLER_TYPE) {
        return ERROR_NONE;
    }
    return spi_controller_lock(parent);
}

void spi_controller_unlock_bus_of(Device* device) {
    Device* parent = device_get_parent(device);
    if (parent == nullptr || device_get_type(parent) != &SPI_CONTROLLER_TYPE) {
        return;
    }
    spi_controller_unlock(parent);
}

const DeviceType SPI_CONTROLLER_TYPE {
    .name = "spi-controller"
};

}
