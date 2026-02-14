// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/device.h>

#define SPI_DRIVER_API(driver) ((struct SpiControllerApi*)driver->api)

extern "C" {

error_t spi_controller_lock(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->lock(device);
}

error_t spi_controller_try_lock(struct Device* device, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->try_lock(device, timeout);
}

error_t spi_controller_unlock(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return SPI_DRIVER_API(driver)->unlock(device);
}

const struct DeviceType SPI_CONTROLLER_TYPE {
    .name = "spi-controller"
};

}
