#pragma once

#ifdef ESP_PLATFORM

#include "esp_vfs_fat.h"

/** The wear-levelling handle of the mounted flash FAT volume (esp32_flash_fatfs driver), or WL_INVALID_HANDLE if there is none. */
wl_handle_t get_data_partition_wl_handle();

#endif // ESP_PLATFORM
