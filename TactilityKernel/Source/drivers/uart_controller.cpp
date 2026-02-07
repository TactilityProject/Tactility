// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/uart_controller.h>
#include <tactility/error.h>
#include <tactility/device.h>
#include <tactility/time.h>

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

error_t uart_controller_read_until(struct Device* device, uint8_t* buffer, size_t buffer_size, uint8_t until_byte, bool add_null_terminator, size_t* bytes_read, TickType_t timeout) {
    size_t read_count = 0;
    TickType_t start_time = get_ticks();
    uint8_t* buffer_write_ptr = buffer;
    uint8_t* buffer_limit = buffer + buffer_size;
    if (add_null_terminator) {
        buffer_limit--;
    }
    TickType_t timeout_left = timeout;
    error_t result = ERROR_NONE;

    while (buffer_write_ptr < buffer_limit) {
        result = uart_controller_read_byte(device, buffer_write_ptr, timeout_left);
        if (result != ERROR_NONE) {
            break;
        }

        read_count++;

        if (*buffer_write_ptr == until_byte) {
            if (add_null_terminator) {
                buffer_write_ptr++;
                *buffer_write_ptr = 0x00U;
            }
            break;
        }

        buffer_write_ptr++;

        TickType_t now = get_ticks();
        if (now > (start_time + timeout)) {
            result = ERROR_TIMEOUT;
            break;
        } else {
            timeout_left = timeout - (now - start_time);
        }
    }

    if (bytes_read != nullptr) {
        *bytes_read = read_count;
    }

    return result;
}

error_t uart_controller_get_available(struct Device* device, size_t* available) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->get_available(device, available);
}

error_t uart_controller_set_config(struct Device* device, const struct UartConfig* config) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->set_config(device, config);
}

error_t uart_controller_get_config(struct Device* device, struct UartConfig* config) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->get_config(device, config);
}

error_t uart_controller_open(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->open(device);
}

error_t uart_controller_close(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->close(device);
}

bool uart_controller_is_open(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->is_open(device);
}

error_t uart_controller_flush_input(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return UART_DRIVER_API(driver)->flush_input(device);
}

const struct DeviceType UART_CONTROLLER_TYPE {
    .name = "uart-controller"
};

}
