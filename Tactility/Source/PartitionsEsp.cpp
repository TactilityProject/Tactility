#ifdef ESP_PLATFORM

#include <Tactility/PartitionsEsp.h>
#include <Tactility/Logger.h>

#include <esp_vfs_fat.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <tactility/error.h>
#include <tactility/filesystem/file_system.h>

#include "esp_system.h"

namespace tt {

static const auto LOGGER = Logger("Partitions");

static wl_handle_t data_wl_handle = WL_INVALID_HANDLE;
static wl_handle_t system_wl_handle = WL_INVALID_HANDLE;

static bool data_mounted = false;
static bool system_mounted = false;

static error_t mount_data(void* data) {
    (void)data;
    return data_mounted ? ERROR_NONE : ERROR_UNDEFINED;
}

static error_t unmount_data(void* data) {
    (void)data;
    return ERROR_NOT_SUPPORTED;
}

static bool is_mounted_data(void* data) {
    (void)data;
    return data_mounted;
}

static error_t get_path_data(void* data, char* out_path, size_t out_path_size) {
    (void)data;
    const char* path = "/data";
    if (strlen(path) >= out_path_size) return ERROR_BUFFER_OVERFLOW;
    strncpy(out_path, path, out_path_size);
    return ERROR_NONE;
}

static error_t mount_system(void* data) {
    (void)data;
    return system_mounted ? ERROR_NONE : ERROR_UNDEFINED;
}

static error_t unmount_system(void* data) {
    (void)data;
    return ERROR_NOT_SUPPORTED;
}

static bool is_mounted_system(void* data) {
    (void)data;
    return system_mounted;
}

static error_t get_path_system(void* data, char* out_path, size_t out_path_size) {
    (void)data;
    const char* path = "/system";
    if (strlen(path) >= out_path_size) return ERROR_BUFFER_OVERFLOW;
    strncpy(out_path, path, out_path_size);
    return ERROR_NONE;
}

FileSystemApi data_fs_api = {
    .mount = mount_data,
    .unmount = unmount_data,
    .is_mounted = is_mounted_data,
    .get_path = get_path_data
};

FileSystemApi system_fs_api = {
    .mount = mount_system,
    .unmount = unmount_system,
    .is_mounted = is_mounted_system,
    .get_path = get_path_system
};

static FileSystem* data_fs = nullptr;
static FileSystem* system_fs = nullptr;

static esp_err_t initNvsFlashSafely() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    return result;
}

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

    esp_err_t err = initNvsFlashSafely();
    if (err != ESP_OK) {
        LOGGER.error("Failed to init NVS flash");
        return err;
    }

    const esp_partition_t* system_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "system"
    );
    const esp_partition_t* data_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "data"
    );

    if (system_partition == nullptr) {
        LOGGER.error("System partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    if (data_partition == nullptr) {
        LOGGER.error("Data partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    LOGGER.info("System partition: offset=0x%x size=0x%x", system_partition->address, system_partition->size);
    LOGGER.info("Data partition: offset=0x%x size=0x%x", data_partition->address, data_partition->size);

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 4096,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    err = esp_vfs_fat_spiflash_mount_rw_wl("/system", "system", &mount_config, &system_wl_handle);
    if (err != ESP_OK) {
        LOGGER.error("Failed to mount system partition: %s", esp_err_to_name(err));
    } else {
        LOGGER.info("System partition mounted at /system");
        system_mounted = true;
        system_fs = file_system_add(&system_fs_api, nullptr);
    }

    err = esp_vfs_fat_spiflash_mount_rw_wl("/data", "data", &mount_config, &data_wl_handle);
    if (err != ESP_OK) {
        LOGGER.error("Failed to mount data partition: %s", esp_err_to_name(err));
    } else {
        LOGGER.info("Data partition mounted at /data");
        data_mounted = true;
        data_fs = file_system_add(&data_fs_api, nullptr);
    }

    return ESP_OK;
}

} // namespace

#endif // ESP_PLATFORM
