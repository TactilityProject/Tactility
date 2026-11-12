// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#include <tactility/drivers/bluetooth.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Length of the hexadecimal representation of a BtAddr, excluding the null terminator */
#define BLUETOOTH_ADDR_HEX_LENGTH (BT_ADDR_LEN * 2)

struct BluetoothPairedDevice {
    BtAddr addr;
    char name[BT_NAME_MAX + 1];
    bool auto_connect;
    /** Profile used to pair (BtProfileId value) */
    int profile;
};

/** Writes the address as lower case hex without separators. out_hex must hold BLUETOOTH_ADDR_HEX_LENGTH + 1 bytes. */
void bluetooth_addr_to_hex(const BtAddr addr, char* out_hex);

/** Parses the output of bluetooth_addr_to_hex(). @return false when hex is not a valid address */
bool bluetooth_hex_to_addr(const char* hex, BtAddr out_addr);

bool bluetooth_paired_device_exists(const BtAddr addr);

/** @retval ERROR_NOT_FOUND when the device was never saved */
error_t bluetooth_paired_device_load(const BtAddr addr, struct BluetoothPairedDevice* device);

error_t bluetooth_paired_device_save(const struct BluetoothPairedDevice* device);

/** @retval ERROR_NOT_FOUND when the device was never saved */
error_t bluetooth_paired_device_remove(const BtAddr addr);

/**
 * Calls on_device for every saved device.
 * @param[in] on_device return false to stop iterating
 */
void bluetooth_paired_device_for_each(void* context, bool (*on_device)(const struct BluetoothPairedDevice* device, void* context));

#ifdef __cplusplus
}
#endif
