// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/esp32_flash_fatfs.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>
#include <tactility/partitions_esp32.h>

#include <esp_vfs_fat.h>

#include <cstring>
#include <new>

constexpr auto* TAG = "esp32_flash_fatfs";

#define GET_CONFIG(device) ((const struct Esp32FlashFatfsConfig*)(device)->config)
#define GET_DATA(device) ((struct Esp32FlashFatfsData*)device_get_driver_data(device))

namespace {

size_t getSectorSize() {
#if defined(CONFIG_FATFS_SECTOR_512)
    return 512;
#elif defined(CONFIG_FATFS_SECTOR_1024)
    return 1024;
#elif defined(CONFIG_FATFS_SECTOR_2048)
    return 2048;
#elif defined(CONFIG_FATFS_SECTOR_4096)
    return 4096;
#else
#error Not implemented
#endif
}

// The wear-levelling handle of the mounted volume, for USB mass storage
wl_handle_t mounted_wl_handle = WL_INVALID_HANDLE;

} // namespace

extern "C" {

struct Esp32FlashFatfsData {
    const Esp32FlashFatfsConfig* config;
    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    bool mounted = false;
    FileSystem* file_system = nullptr;
};

static error_t fs_mount(void* data) {
    auto* fs_data = static_cast<Esp32FlashFatfsData*>(data);
    if (fs_data->mounted) {
        return ERROR_NONE;
    }

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = getSectorSize(),
        .disk_status_check_enable = false,
        .use_one_fat = true,
    };

    LOG_I(TAG, "Mounting %s", fs_data->config->mount_path);
    esp_err_t result = esp_vfs_fat_spiflash_mount_rw_wl(fs_data->config->mount_path, fs_data->config->partition_label, &mount_config, &fs_data->wl_handle);
    if (result != ESP_OK) {
        LOG_E(TAG, "Failed to mount %s (%s)", fs_data->config->mount_path, esp_err_to_name(result));
        return ERROR_RESOURCE;
    }

    fs_data->mounted = true;
    mounted_wl_handle = fs_data->wl_handle;
    return ERROR_NONE;
}

static error_t fs_unmount(void* data) {
    auto* fs_data = static_cast<Esp32FlashFatfsData*>(data);
    if (!fs_data->mounted) {
        return ERROR_NONE;
    }

    LOG_I(TAG, "Unmounting %s", fs_data->config->mount_path);
    if (esp_vfs_fat_spiflash_unmount_rw_wl(fs_data->config->mount_path, fs_data->wl_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to unmount %s", fs_data->config->mount_path);
        return ERROR_RESOURCE;
    }

    fs_data->mounted = false;
    fs_data->wl_handle = WL_INVALID_HANDLE;
    mounted_wl_handle = WL_INVALID_HANDLE;
    return ERROR_NONE;
}

static bool fs_is_mounted(void* data) {
    return static_cast<Esp32FlashFatfsData*>(data)->mounted;
}

static error_t fs_get_path(void* data, char* out_path, size_t out_path_size) {
    const char* mount_path = static_cast<Esp32FlashFatfsData*>(data)->config->mount_path;
    if (strlen(mount_path) >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(out_path, mount_path);
    return ERROR_NONE;
}

static const FileSystemApi fs_api = {
    .mount = fs_mount,
    .unmount = fs_unmount,
    .is_mounted = fs_is_mounted,
    .get_path = fs_get_path,
};

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    auto* data = new (std::nothrow) Esp32FlashFatfsData();
    if (data == nullptr) return ERROR_OUT_OF_MEMORY;
    data->config = GET_CONFIG(device);

    data->file_system = file_system_add(data->config->partition_label, &fs_api, data);
    if (data->file_system == nullptr) {
        delete data;
        return ERROR_OUT_OF_MEMORY;
    }
    file_system_set_owner(data->file_system, device);
    device_set_driver_data(device, data);

    if (file_system_mount(data->file_system) != ERROR_NONE) {
        // The failure is reported at boot when the root path isn't available
        LOG_E(TAG, "Failed to mount the filesystem");
    }
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    auto* data = GET_DATA(device);
    if (data == nullptr) return ERROR_NONE;

    if (file_system_unmount(data->file_system) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }
    file_system_remove(data->file_system);
    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

Driver esp32_flash_fatfs_driver = {
    .name = "esp32_flash_fatfs",
    .compatible = (const char*[]) { "espressif,esp32-flash-fatfs", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .internal = nullptr
};

} // extern "C"

wl_handle_t get_data_partition_wl_handle() {
    return mounted_wl_handle;
}
