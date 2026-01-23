#pragma once

#include <string>
#include "UartCompat.h"

namespace tt::hal::uart {

#ifdef ESP_PLATFORM

struct Configuration {
    /** The unique name for this UART */
    std::string name;
    /** The port identifier (e.g. UART_NUM_0) */
    uart_port_t port;
    /** Receive GPIO pin */
    gpio_num_t rxPin;
    /** Transmit GPIO pin */
    gpio_num_t txPin;
    /** Read-To-Send GPIO pin */
    gpio_num_t rtsPin;
    /** Clear-To-Send Send GPIO pin */
    gpio_num_t ctsPin;
    /** Receive buffer size in bytes */
    unsigned int rxBufferSize;
    /** Transmit buffer size in bytes */
    unsigned int txBufferSize;
    /** Native configuration */
    uart_config_t config;
};

#else

struct Configuration {
    /** The unique name for this UART */
    std::string name;
    /** The port identifier (e.g. UART_NUM_0) */
    uart_port_t port;
    /** Initial baud rate in bits per second */
    uint32_t baudRate;
};

#endif

}
