// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/uart_controller.h>
#include <tactility/error.h>
#include <tactility/device.h>

#define UART_DRIVER_API(driver) ((struct UartControllerApi*)driver->api)

extern "C" {

error_t uart_controller_read_byte(struct Device* device, uint8_t* out, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->read_byte(device, out, timeout);
}

error_t uart_controller_write_byte(struct Device* device, uint8_t out, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->write_byte(device, out, timeout);
}

error_t uart_controller_write_bytes(struct Device* device, const uint8_t* buffer, size_t buffer_size, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->write_bytes(device, buffer, buffer_size, timeout);
}

error_t uart_controller_read_bytes(struct Device* device, uint8_t* buffer, size_t buffer_size, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->read_bytes(device, buffer, buffer_size, timeout);
}

int uart_controller_available(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->available(device);
}

error_t uart_controller_set_config(struct Device* device, const struct UartConfig* config) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->set_config(device, config);
}

error_t uart_controller_get_config(struct Device* device, struct UartConfig* config) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->get_config(device, config);
}

error_t uart_controller_reset(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->reset(device);
}

const struct DeviceType UART_CONTROLLER_TYPE {
    .name = "uart-controller"
};

}
