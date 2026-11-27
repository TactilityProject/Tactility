// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the firmware of this device as FIRMWARE_ID_SYSTEM, which writes updates to the next OTA app partition.
 * @retval ERROR_NOT_SUPPORTED the partition table has no OTA app partitions
 */
error_t esp32_system_firmware_register(void);

#ifdef __cplusplus
}
#endif
