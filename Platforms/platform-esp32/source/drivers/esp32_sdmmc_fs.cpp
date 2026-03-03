// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/esp32_sdmmc.h>
#include <tactility/drivers/file_system.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <new>
#include <sdmmc_cmd.h>
#include <string>

#if SOC_SD_PWR_CTRL_SUPPORTED
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#endif

#define TAG "esp32_sdmmc_fs"

#define GET_DATA(device) ((struct Esp32SdmmcFsInternal*)device_get_driver_data(device))
// We re-use the config from the parent device
#define GET_CONFIG(device) ((const struct Esp32SdmmcConfig*)device->config)

struct Esp32SdmmcFsInternal {
    std::string mount_path {};
    sdmmc_card_t* card = nullptr;
#if SOC_SD_PWR_CTRL_SUPPORTED
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = nullptr;
#endif
};

static gpio_num_t to_native_pin(GpioPinSpec pin_spec) {
    if (pin_spec.gpio_controller == nullptr) { return GPIO_NUM_NC; }
    return static_cast<gpio_num_t>(pin_spec.pin);
}

extern "C" {

error_t mount(Device* device, const char* mount_path) {
    LOG_I(TAG, "Mounting %s", mount_path);

    auto* data = GET_DATA(device);
    auto* config = GET_CONFIG(device);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 0, // Default is sector size
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

#if SOC_SD_PWR_CTRL_SUPPORTED
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4, // LDO4 is typically used for SDMMC on ESP32-S3
    };
    esp_err_t pwr_err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &data->pwr_ctrl_handle);
    if (pwr_err != ESP_OK) {
        LOG_E(TAG, "Failed to create SD power control driver, err=0x%x", pwr_err);
        return ERROR_NOT_SUPPORTED;
    }
    host.pwr_ctrl_handle = data->pwr_ctrl_handle;
#endif

    uint32_t slot_config_flags = 0;
    if (config->enable_uhs) slot_config_flags |= SDMMC_SLOT_FLAG_UHS1;
    if (config->wp_active_high) slot_config_flags |= SDMMC_SLOT_FLAG_WP_ACTIVE_HIGH;

    sdmmc_slot_config_t slot_config = {
        .clk = to_native_pin(config->pin_clk),
        .cmd = to_native_pin(config->pin_cmd),
        .d0  = to_native_pin(config->pin_d0),
        .d1  = to_native_pin(config->pin_d1),
        .d2  = to_native_pin(config->pin_d2),
        .d3  = to_native_pin(config->pin_d3),
        .d4  = to_native_pin(config->pin_d4),
        .d5  = to_native_pin(config->pin_d5),
        .d6  = to_native_pin(config->pin_d6),
        .d7  = to_native_pin(config->pin_d7),
        .cd  = to_native_pin(config->pin_cd),
        .wp  = to_native_pin(config->pin_wp),
        .width = config->bus_width,
        .flags = slot_config_flags
    };

    esp_err_t result = esp_vfs_fat_sdmmc_mount(mount_path, &host, &slot_config, &mount_config, &data->card);

    if (result != ESP_OK || data->card == nullptr) {
        if (result == ESP_FAIL) {
            LOG_E(TAG, "Mounting failed. Ensure the card is formatted with FAT.");
        } else {
            LOG_E(TAG, "Mounting failed: %s", esp_err_to_name(result));
        }
#if SOC_SD_PWR_CTRL_SUPPORTED
        if (data->pwr_ctrl_handle) {
            sd_pwr_ctrl_del_on_chip_ldo(data->pwr_ctrl_handle);
            data->pwr_ctrl_handle = nullptr;
        }
#endif
        return ERROR_UNDEFINED;
    }

    LOG_I(TAG, "Mounted %s", mount_path);
    data->mount_path = mount_path;

    return ERROR_NONE;
}

error_t unmount(Device* device) {
    auto* data = GET_DATA(device);

    if (esp_vfs_fat_sdcard_unmount(data->mount_path.c_str(), data->card) != ESP_OK) {
        LOG_E(TAG, "Unmount failed for %s", data->mount_path.c_str());
        return ERROR_UNDEFINED;
    }

    LOG_I(TAG, "Unmounted %s", data->mount_path.c_str());
    data->mount_path = "";
    data->card = nullptr;

#if SOC_SD_PWR_CTRL_SUPPORTED
    if (data->pwr_ctrl_handle) {
        sd_pwr_ctrl_del_on_chip_ldo(data->pwr_ctrl_handle);
        data->pwr_ctrl_handle = nullptr;
    }
#endif

    return ERROR_NONE;
}

bool is_mounted(Device* device) {
    const auto* data = GET_DATA(device);
    return data != nullptr && data->card != nullptr;
}

error_t get_mount_path(Device* device, char* out_path, size_t out_path_size) {
    const auto* data = GET_DATA(device);
    if (data == nullptr || data->card == nullptr) return ERROR_INVALID_STATE;
    if (data->mount_path.size() >= out_path_size) return ERROR_BUFFER_OVERFLOW;
    strncpy(out_path, data->mount_path.c_str(), out_path_size);
    return ERROR_NONE;
}

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    auto* data = new (std::nothrow) Esp32SdmmcFsInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;

    device_set_driver_data(device, data);

    if (mount(device, "/sdcard") != ERROR_NONE) {
        LOG_E(TAG, "Failed to mount SD card");
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = GET_DATA(device);

    if (is_mounted(device)) {
        if (unmount(device) != ERROR_NONE) {
            LOG_E(TAG, "Failed to unmount SD card");
        }
    }

    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

extern Module platform_esp32_module;

static const FileSystemApi sdmmc_fs_api = {
    .mount = mount,
    .unmount = unmount,
    .is_mounted = is_mounted,
    .get_mount_path = get_mount_path
};

Driver esp32_sdmmc_fs_driver = {
    .name = "esp32_sdmmc_fs",
    .compatible = (const char*[]) { "espressif,esp32-sdmmc-fs", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &sdmmc_fs_api,
    .device_type = &FILE_SYSTEM_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
