#ifdef ESP_PLATFORM

#include <tactility/partitions_esp32.h>

#include <esp_vfs_fat.h>
#include <nvs_flash.h>
#include <tactility/error.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#include <cstring>

namespace {

constexpr auto* TAG = "platform-esp32";

// region file_system stub

struct PartitionFsData {
    const char* path;
};

error_t mount(void* data) {
    return ERROR_NOT_SUPPORTED;
}

error_t unmount(void* data) {
    return ERROR_NOT_SUPPORTED;
}

bool is_mounted(void* data) {
    return true;
}

error_t get_path(void* data, char* out_path, size_t out_path_size) {
    auto* fs_data = static_cast<PartitionFsData*>(data);
    if (strlen(fs_data->path) >= out_path_size) return ERROR_BUFFER_OVERFLOW;
    strncpy(out_path, fs_data->path, out_path_size);
    return ERROR_NONE;
}

const FileSystemApi partition_fs_api = {
    .mount = mount,
    .unmount = unmount,
    .is_mounted = is_mounted,
    .get_path = get_path
};

// endregion file_system stub

esp_err_t initNvsFlashSafely() {
    LOG_I(TAG, "Init NVS");
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    return result;
}

wl_handle_t data_wl_handle = WL_INVALID_HANDLE;

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

} // namespace

wl_handle_t get_data_partition_wl_handle() {
    return data_wl_handle;
}

error_t platform_esp32_start_partitions() {
    LOG_I(TAG, "Init partitions");
    ESP_ERROR_CHECK(initNvsFlashSafely());

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = getSectorSize(),
        .disk_status_check_enable = false,
        .use_one_fat = true,
    };

    auto system_result = esp_vfs_fat_spiflash_mount_ro("/system", "system", &mount_config);
    if (system_result != ESP_OK) {
        LOG_E(TAG, "Failed to mount /system (%s)", esp_err_to_name(system_result));
        return ERROR_RESOURCE;
    }
    LOG_I(TAG, "Mounted /system");
    static auto system_fs_data = PartitionFsData("/system");
    file_system_add("system", &partition_fs_api, &system_fs_data);

#ifdef CONFIG_TT_USER_DATA_LOCATION_INTERNAL
    auto data_result = esp_vfs_fat_spiflash_mount_rw_wl("/data", "data", &mount_config, &data_wl_handle);
    if (data_result != ESP_OK) {
        LOG_E(TAG, "Failed to mount /data (%s)", esp_err_to_name(data_result));
        return ERROR_RESOURCE;
    }

    LOG_I(TAG, "Mounted /data");
    static auto data_fs_data = PartitionFsData("/data");
    file_system_add("data", &partition_fs_api, &data_fs_data);
#endif

    return ERROR_NONE;
}

#endif // ESP_PLATFORM
