#pragma once

#include <dirent.h>
#include <vector>

namespace tt::file {

constexpr auto* SYSTEM_PARTITION_NAME = "system";

#ifdef ESP_PLATFORM
constexpr auto* MOUNT_POINT_SYSTEM = "/system";
#else
constexpr auto* MOUNT_POINT_SYSTEM = "system";
#endif

constexpr auto* DATA_PARTITION_NAME = "data";

#ifdef ESP_PLATFORM
constexpr auto* MOUNT_POINT_DATA = "/data";
#else
constexpr auto* MOUNT_POINT_DATA = "data";
#endif

std::vector<dirent> getFileSystemDirents();

} // namespace
