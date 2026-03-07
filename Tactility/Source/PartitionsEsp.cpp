#ifdef ESP_PLATFORM

#include <Tactility/PartitionsEsp.h>
#include <Tactility/Logger.h>

#include <esp_vfs_fat.h>
#include <nvs_flash.h>
#include <tactility/error.h>
#include <tactility/filesystem/file_system.h>

namespace tt {

static const auto LOGGER = Logger("Partitions");

// region file_system stub

struct PartitionFsData {
    const char* path;
};

static error_t mount(void* data) {
    return ERROR_NOT_SUPPORTED;
}

static error_t unmount(void* data) {
    return ERROR_NOT_SUPPORTED;
}

static bool is_mounted(void* data) {
    return true;
}

static error_t get_path(void* data, char* out_path, size_t out_path_size) {
    auto* fs_data = static_cast<PartitionFsData*>(data);
    if (strlen(fs_data->path) >= out_path_size) return ERROR_BUFFER_OVERFLOW;
    strncpy(out_path, fs_data->path, out_path_size);
    return ERROR_NONE;
}

FileSystemApi partition_fs_api = {
    .mount = mount,
    .unmount = unmount,
    .is_mounted = is_mounted,
    .get_path = get_path
};

// endregion file_system stub

static esp_err_t initNvsFlashSafely() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    return result;
}

static wl_handle_t data_wl_handle = WL_INVALID_HANDLE;

wl_handle_t getDataPartitionWlHandle() {
    return data_wl_handle;
}

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

esp_err_t initPartitionsEsp() {
    LOGGER.info("Init partitions");
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
        LOGGER.error("Failed to mount /system ({})", esp_err_to_name(system_result));
    } else {
        LOGGER.info("Mounted /system");
        static auto system_fs_data = PartitionFsData("/system");
        file_system_add(&partition_fs_api, &system_fs_data);
    }

    auto data_result = esp_vfs_fat_spiflash_mount_rw_wl("/data", "data", &mount_config, &data_wl_handle);
    if (data_result != ESP_OK) {
        LOGGER.error("Failed to mount /data ({})", esp_err_to_name(data_result));
    } else {
        LOGGER.info("Mounted /data");
        static auto data_fs_data = PartitionFsData("/data");
        file_system_add(&partition_fs_api, &data_fs_data);
    }

    return system_result == ESP_OK && data_result == ESP_OK;
}

} // namespace

#endif // ESP_PLATFORM
