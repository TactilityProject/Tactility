#pragma once

#ifdef ESP_PLATFORM

#include "esp_vfs_fat.h"

/** The "data" partition's wear-levelling handle, or WL_INVALID_HANDLE if it wasn't mounted (e.g.
 * CONFIG_TT_USER_DATA_LOCATION_SD). Set once by platform_esp32_start_partitions(). */
wl_handle_t get_data_partition_wl_handle();

#endif // ESP_PLATFORM
