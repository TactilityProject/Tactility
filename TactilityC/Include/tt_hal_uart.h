#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * WARNING: THIS API IS NON-FUNCTIONAL AND DEPRECATED.
 * IT WILL BE REMOVED IN A FUTURE RELEASE ONCE OFFICIAL APPS ARE MIGRATED.
* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 */

/**
 * @file tt_hal_uart.h
 * @brief C HAL interface for UART devices used by Tactility C modules.
 *
 * This header exposes a minimal, C-compatible UART API that mirrors the higher-level
 * C++ UART interface (see Tactility/hal/uart).
 *
 * General notes:
 * - Start the UART before I/O using tt_hal_uart_start(); stop it with tt_hal_uart_stop().
 */

typedef void* UartHandle; /**< Opaque handle to an underlying UART instance. */

/**
 * @brief Get the number of UART devices available on this platform.
 * @return Count of discoverable UARTs (0 if none).
 */
size_t tt_hal_uart_get_count();

/**
 * @brief Get the user-friendly name of a UART by index.
 * @param index Zero-based UART index in the range [0, tt_hal_uart_get_count()).
 * @param[out] name Destination buffer to receive a null-terminated name.
 * @param nameSizeLimit Size in bytes of the destination buffer. The name will be
 *                      truncated to fit and always null-terminated if the size
 *                      is greater than 0.
 * @return true if a name was written to the buffer; false if the index is out of range
 *         or on other failure.
 */
bool tt_hal_uart_get_name(size_t index, char* name, size_t nameSizeLimit);

/**
 * @brief Allocate an opaque UART handle by index.
 *
 * Allocation does not start the hardware; call tt_hal_uart_start() to begin I/O.
 *
 * @param index Zero-based UART index.
 * @return A valid UartHandle on success; NULL on failure (e.g., invalid index or already in use).
 */
UartHandle tt_hal_uart_alloc(size_t index);

/**
 * @brief Release a previously allocated UART handle and any associated resources.
 * @param handle Handle returned by tt_hal_uart_alloc()
 */
void tt_hal_uart_free(UartHandle handle);

/**
 * @brief Start the UART so it can perform I/O.
 * @param handle A valid UART handle.
 * @return true on success; false on failure.
 */
bool tt_hal_uart_start(UartHandle handle);

/**
 * @brief Query whether the UART has been started.
 * @param handle A valid UART handle.
 * @return true if started; false otherwise.
 */
bool tt_hal_uart_is_started(UartHandle handle);

/**
 * @brief Stop the UART
 * @param handle A valid UART handle.
 * @return true on success; false on failure.
 */
bool tt_hal_uart_stop(UartHandle handle);

/**
 * @brief Read up to bufferSize bytes into buffer.
 *
 * This call may block up to timeout ticks waiting for data. It returns the actual
 * number of bytes placed into the buffer, which can be less than bufferSize if
 * fewer bytes became available before the timeout expired.
 *
 * @param handle A valid UART handle.
 * @param[out] buffer Destination buffer.
 * @param bufferSize Capacity of the destination buffer in bytes.
 * @param timeout Maximum time to wait in ticks. Use 0 for non-blocking; use TT_MAX_TICKS
 *                to wait indefinitely.
 * @return The number of bytes read (0 on timeout with no data). Never exceeds bufferSize.
 */
size_t tt_hal_uart_read_bytes(UartHandle handle, char* buffer, size_t bufferSize, TickType_t timeout);

/**
 * @brief Read a single byte.
 *
 * @param handle A valid UART handle.
 * @param[out] output Where to store the read byte.
 * @param timeout Maximum time to wait in ticks. Use 0 for non-blocking; use TT_MAX_TICKS
 *                to wait indefinitely.
 * @return true if a byte was read and stored in output; false on timeout or failure.
 */
bool tt_hal_uart_read_byte(UartHandle handle, char* output, TickType_t timeout);

/**
 * @brief Write up to bufferSize bytes from buffer.
 *
 * This call may block up to timeout ticks waiting for transmit queue space. It returns
 * the number of bytes accepted for transmission.
 *
 * @param handle A valid UART handle.
 * @param[in] buffer Source buffer containing bytes to write.
 * @param bufferSize Number of bytes to write from buffer.
 * @param timeout Maximum time to wait in ticks. Use 0 for non-blocking; use TT_MAX_TICKS
 *                to wait indefinitely.
 * @return The number of bytes written (may be less than bufferSize on timeout).
 */
size_t tt_hal_uart_write_bytes(UartHandle handle, const char* buffer, size_t bufferSize, TickType_t timeout);

/**
 * @brief Get the number of bytes currently available to read without blocking.
 * @param handle A valid UART handle.
 * @return The count of bytes available in the receive buffer.
 */
size_t tt_hal_uart_available(UartHandle handle);

/**
 * @brief Set the UART baud rate.
 * @param handle A valid UART handle.
 * @param baud_rate Desired baud rate in bits per second (e.g., 115200).
 * @return true on success; false if the rate is unsupported or on error.
 */
bool tt_hal_uart_set_baud_rate(UartHandle handle, size_t baud_rate);

/**
 * @brief Get the current UART baud rate.
 * @param handle A valid UART handle.
 * @return The configured baud rate in bits per second.
 */
uint32_t tt_hal_uart_get_baud_rate(UartHandle handle);

/**
 * @brief Flush the UART input (receive) buffer, discarding any unread data.
 * @param handle A valid UART handle.
 */
void tt_hal_uart_flush_input(UartHandle handle);

#ifdef __cplusplus
}
#endif
