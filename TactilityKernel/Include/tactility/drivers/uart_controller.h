// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include <tactility/freertos/freertos.h>
#include <tactility/error.h>

struct Device;

/**
 * @brief UART parity modes
 */
enum UartParity {
    UART_CONTROLLER_PARITY_DISABLE = 0x0,
    UART_CONTROLLER_PARITY_EVEN = 0x1,
    UART_CONTROLLER_PARITY_ODD = 0x2,
};

/**
 * @brief UART data bits
 */
enum UartDataBits {
    UART_CONTROLLER_DATA_5_BITS = 0x0,
    UART_CONTROLLER_DATA_6_BITS = 0x1,
    UART_CONTROLLER_DATA_7_BITS = 0x2,
    UART_CONTROLLER_DATA_8_BITS = 0x3,
};

/**
 * @brief UART stop bits
 */
enum UartStopBits {
    UART_CONTROLLER_STOP_BITS_1 = 0x1,
    UART_CONTROLLER_STOP_BITS_1_5 = 0x2,
    UART_CONTROLLER_STOP_BITS_2 = 0x3,
};

/**
 * @brief UART Config
 */
struct UartConfig {
    uint32_t baud_rate;
    enum UartDataBits data_bits;
    enum UartParity parity;
    enum UartStopBits stop_bits;
    int8_t tx_pin;
    int8_t rx_pin;
    int8_t cts_pin;
    int8_t rts_pin;
};

/**
 * @brief API for UART controller drivers.
 */
struct UartControllerApi {
    /**
     * @brief Reads a single byte from UART.
     * @param[in] device the UART controller device
     * @param[out] out the buffer to store the read byte
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the read operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read_byte)(struct Device* device, uint8_t* out, TickType_t timeout);

    /**
     * @brief Writes a single byte to UART.
     * @param[in] device the UART controller device
     * @param[in] out the byte to write
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the write operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write_byte)(struct Device* device, uint8_t out, TickType_t timeout);

    /**
     * @brief Writes multiple bytes to UART.
     * @param[in] device the UART controller device
     * @param[in] buffer the buffer containing the data to write
     * @param[in] buffer_size the number of bytes to write
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the write operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write_bytes)(struct Device* device, const uint8_t* buffer, size_t buffer_size, TickType_t timeout);

    /**
     * @brief Reads multiple bytes from UART.
     * @param[in] device the UART controller device
     * @param[out] buffer the buffer to store the read data
     * @param[in] buffer_size the number of bytes to read
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the read operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read_bytes)(struct Device* device, uint8_t* buffer, size_t buffer_size, TickType_t timeout);

    /**
     * @brief Returns the number of bytes available for reading.
     * @param[in] device the UART controller device
     * @return the number of bytes available, or a negative error code on failure
     */
    int (*available)(struct Device* device);

    /**
     * @brief Sets the UART configuration.
     * @param[in] device the UART controller device
     * @param[in] config the new configuration
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*set_config)(struct Device* device, const struct UartConfig* config);

    /**
     * @brief Gets the current UART configuration.
     * @param[in] device the UART controller device
     * @param[out] config the buffer to store the current configuration
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*get_config)(struct Device* device, struct UartConfig* config);

    /**
     * @brief Opens the UART controller.
     * @param[in] device the UART controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*open)(struct Device* device);

    /**
     * @brief Closes the UART controller.
     * @param[in] device the UART controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*close)(struct Device* device);

    /**
     * @brief Checks if the UART controller is open.
     * @param[in] device the UART controller device
     * @return true if open, false otherwise
     */
    bool (*is_open)(struct Device* device);

    /**
     * @brief Flushes the UART input buffer.
     * @param[in] device the UART controller device
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*flush_input)(struct Device* device);
};

/**
 * @brief Reads a single byte from UART using the specified controller.
 */
error_t uart_controller_read_byte(struct Device* device, uint8_t* out, TickType_t timeout);

/**
 * @brief Writes a single byte to UART using the specified controller.
 */
error_t uart_controller_write_byte(struct Device* device, uint8_t out, TickType_t timeout);

/**
 * @brief Writes multiple bytes to UART using the specified controller.
 */
error_t uart_controller_write_bytes(struct Device* device, const uint8_t* buffer, size_t buffer_size, TickType_t timeout);

/**
 * @brief Reads multiple bytes from UART using the specified controller.
 */
error_t uart_controller_read_bytes(struct Device* device, uint8_t* buffer, size_t buffer_size, TickType_t timeout);

/**
 * @brief Returns the number of bytes available for reading using the specified controller.
 */
int uart_controller_available(struct Device* device);

/**
 * @brief Sets the UART configuration using the specified controller.
 */
error_t uart_controller_set_config(struct Device* device, const struct UartConfig* config);

/**
 * @brief Gets the current UART configuration using the specified controller.
 */
error_t uart_controller_get_config(struct Device* device, struct UartConfig* config);

/**
 * @brief Opens the UART controller using the specified controller.
 */
error_t uart_controller_open(struct Device* device);

/**
 * @brief Closes the UART controller using the specified controller.
 */
error_t uart_controller_close(struct Device* device);

/**
 * @brief Checks if the UART controller is open using the specified controller.
 */
bool uart_controller_is_open(struct Device* device);

/**
 * @brief Resets the UART controller using the specified controller.
 */
error_t uart_controller_reset(struct Device* device);

/**
 * @brief Flushes the UART input buffer using the specified controller.
 */
error_t uart_controller_flush_input(struct Device* device);

extern const struct DeviceType UART_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
