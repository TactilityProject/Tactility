// SPDX-License-Identifier: Apache-2.0
#include <soc/soc_caps.h>
#if SOC_SDMMC_HOST_SUPPORTED
#include <tactility/device.h>
#include <tactility/drivers/esp32_sdmmc.h>
#include <tactility/drivers/esp32_sdmmc_fs.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <string>

#if SOC_SD_PWR_CTRL_SUPPORTED
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#endif

#define TAG "esp32_sdmmc_fs"

#define GET_DATA(data) static_cast<Esp32SdmmcFsData*>(data)

struct Esp32SdmmcFsData {
    const std::string mount_path;
    const Esp32SdmmcConfig* config;
    sdmmc_card_t* card;
#if SOC_SD_PWR_CTRL_SUPPORTED
    sd_pwr_ctrl_handle_t pwr_ctrl_handle;
#endif

    Esp32SdmmcFsData(const Esp32SdmmcConfig* config, const std::string& mount_path) :
        mount_path(mount_path),
        config(config),
        card(nullptr)
#if SOC_SD_PWR_CTRL_SUPPORTED
        ,pwr_ctrl_handle(nullptr)
#endif
    {}
};

static gpio_num_t to_native_pin(GpioPinSpec pin_spec) {
    if (pin_spec.gpio_controller == nullptr) { return GPIO_NUM_NC; }
    return static_cast<gpio_num_t>(pin_spec.pin);
}

extern "C" {

static error_t get_path(void* data, char* out_path, size_t out_path_size);

Esp32SdmmcHandle esp32_sdmmc_fs_alloc(const Esp32SdmmcConfig* config, const char* mount_path) {
    return new(std::nothrow) Esp32SdmmcFsData(config, mount_path);
}

sdmmc_card_t* esp32_sdmmc_fs_get_card(Esp32SdmmcHandle handle) {
    return GET_DATA(handle)->card;
}

void esp32_sdmmc_fs_free(Esp32SdmmcHandle handle) {
    auto* fs_data = GET_DATA(handle);
    delete fs_data;
}

static error_t mount(void* data) {
    auto* fs_data = GET_DATA(data);
    auto* config = fs_data->config;

    LOG_I(TAG, "Mounting %s", fs_data->mount_path.c_str());

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 0, // Default is sector size
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

#if SOC_SD_PWR_CTRL_SUPPORTED
    // Treat non-positive values as disabled to remain safe with zero-initialized configs.
    if (config->on_chip_ldo_chan > 0) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = (uint32_t)config->on_chip_ldo_chan,
        };
        esp_err_t pwr_err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &fs_data->pwr_ctrl_handle);
        if (pwr_err != ESP_OK) {
            LOG_E(TAG, "Failed to create SD power control driver, err=0x%x", pwr_err);
            return ERROR_NOT_SUPPORTED;
        }
        host.pwr_ctrl_handle = fs_data->pwr_ctrl_handle;
    }
#endif

    uint32_t slot_config_flags = 0;
    if (config->enable_uhs) slot_config_flags |= SDMMC_SLOT_FLAG_UHS1;
    if (config->wp_active_high) slot_config_flags |= SDMMC_SLOT_FLAG_WP_ACTIVE_HIGH;
    if (config->pullups) slot_config_flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

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

    esp_err_t result = esp_vfs_fat_sdmmc_mount(fs_data->mount_path.c_str(), &host, &slot_config, &mount_config, &fs_data->card);

    if (result != ESP_OK || fs_data->card == nullptr) {
        if (result == ESP_FAIL) {
            LOG_E(TAG, "Mounting failed. Ensure the card is formatted with FAT.");
        } else {
            LOG_E(TAG, "Mounting failed: %s", esp_err_to_name(result));
        }
#if SOC_SD_PWR_CTRL_SUPPORTED
        if (fs_data->pwr_ctrl_handle) {
            sd_pwr_ctrl_del_on_chip_ldo(fs_data->pwr_ctrl_handle);
            fs_data->pwr_ctrl_handle = nullptr;
        }
#endif
        return ERROR_UNDEFINED;
    }

    sdmmc_card_print_info(stdout, fs_data->card);
    LOG_I(TAG, "Mounted %s", fs_data->mount_path.c_str());

    return ERROR_NONE;
}

static error_t unmount(void* data) {
    auto* fs_data = GET_DATA(data);
    LOG_I(TAG, "Unmounting %s", fs_data->mount_path.c_str());

    if (esp_vfs_fat_sdcard_unmount(fs_data->mount_path.c_str(), fs_data->card) != ESP_OK) {
        LOG_E(TAG, "Unmount failed for %s", fs_data->mount_path.c_str());
        return ERROR_UNDEFINED;
    }

    fs_data->card = nullptr;

#if SOC_SD_PWR_CTRL_SUPPORTED
    if (fs_data->pwr_ctrl_handle) {
        sd_pwr_ctrl_del_on_chip_ldo(fs_data->pwr_ctrl_handle);
        fs_data->pwr_ctrl_handle = nullptr;
    }
#endif

    LOG_I(TAG, "Unmounted %s", fs_data->mount_path.c_str());

    return ERROR_NONE;
}

static bool is_mounted(void* data) {
    const auto* fs_data = GET_DATA(data);
    if (fs_data->card == nullptr) return false;
    return sdmmc_get_status(fs_data->card) == ESP_OK;
}

static error_t get_path(void* data, char* out_path, size_t out_path_size) {
    const auto* fs_data = GET_DATA(data);
    if (fs_data->mount_path.size() >= out_path_size) return ERROR_BUFFER_OVERFLOW;
    strncpy(out_path, fs_data->mount_path.c_str(), out_path_size);
    return ERROR_NONE;
}

const FileSystemApi esp32_sdmmc_fs_api = {
    .mount = mount,
    .unmount = unmount,
    .is_mounted = is_mounted,
    .get_path = get_path
};

} // extern "C"
#endif