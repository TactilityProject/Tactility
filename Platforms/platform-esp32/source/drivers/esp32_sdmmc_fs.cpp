// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/esp32_sdmmc.h>
#include <tactility/drivers/file_system.h>
#include <tactility/log.h>

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <new>
#include <sdmmc_cmd.h>
#include <string>
#include <tactility/drivers/gpio_descriptor.h>

#define TAG "esp32_sdmmc_fs"

#define GET_DATA(device) ((struct Esp32SdmmcFsInternal*)device_get_driver_data(device))
// We re-use the config from the parent device
#define GET_CONFIG(device) ((const struct Esp32SdmmcConfig*)device->config)

struct Esp32SdmmcFsInternal {
    std::string mount_path {};
    sdmmc_card_t* card = nullptr;
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
        .max_files = config->max_open_files,
        .allocation_unit_size = 0, // Default is sector size
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

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
        return false;
    }

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

    // TODO: filesystem

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = GET_DATA(device);

    // TODO: filesystem

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
