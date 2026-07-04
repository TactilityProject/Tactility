#pragma once

#ifdef ESP_PLATFORM

#include "esp_err.h"
#include "esp_vfs_fat.h"

namespace tt {

bool initPartitionsEsp();
wl_handle_t getDataPartitionWlHandle();

} // namespace

#endif // ESP_PLATFORM
