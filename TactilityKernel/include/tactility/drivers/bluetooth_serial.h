#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

// ---- BLE Serial (Nordic UART Service) child device ----

/**
 * BLE serial port profile API (Nordic UART Service or equivalent GATT-based SPP).
 * This API is exposed by a child device of the Bluetooth device.
 */
struct BtSerialApi {
    /**
     * Start advertising the BLE serial service and accept incoming connections.
     * @param[in] device the serial child device
     * @return ERROR_NONE on success
     */
    error_t (*start)(struct Device* device);

    /**
     * Stop the BLE serial service and close any active connections.
     * @param[in] device the serial child device
     * @return ERROR_NONE on success
     */
    error_t (*stop)(struct Device* device);

    /**
     * Write data over the BLE serial connection.
     * @param[in] device the serial child device
     * @param[in] data the data to send
     * @param[in] len the number of bytes to send
     * @param[out] written the number of bytes actually written
     * @return ERROR_NONE on success
     */
    error_t (*write)(struct Device* device, const uint8_t* data, size_t len, size_t* written);

    /**
     * Read data from the BLE serial receive buffer.
     * @param[in] device the serial child device
     * @param[out] data the buffer to read into
     * @param[in] max_len the maximum number of bytes to read
     * @param[out] read_out the number of bytes actually read
     * @return ERROR_NONE on success
     */
    error_t (*read)(struct Device* device, uint8_t* data, size_t max_len, size_t* read_out);

    /**
     * @param[in] device the serial child device
     * @return true when a remote device is connected
     */
    bool (*is_connected)(struct Device* device);
};

extern const struct DeviceType BLUETOOTH_SERIAL_TYPE;

/** Find the first ready BLE serial child device. Returns NULL if unavailable. */
struct Device* bluetooth_serial_get_device(void);

error_t bluetooth_serial_start(struct Device* device);
error_t bluetooth_serial_stop(struct Device* device);
error_t bluetooth_serial_write(struct Device* device, const uint8_t* data, size_t len, size_t* written);
error_t bluetooth_serial_read(struct Device* device, uint8_t* data, size_t max_len, size_t* read_out);
bool    bluetooth_serial_is_connected(struct Device* device);

#ifdef __cplusplus
}
#endif
