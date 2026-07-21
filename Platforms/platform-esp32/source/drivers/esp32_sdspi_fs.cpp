// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/esp32_sdspi.h>
#include <tactility/drivers/esp32_sdspi_fs.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>

#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <string>

#define TAG "esp32_sdspi_fs"

#define GET_DATA(data) static_cast<Esp32SdspiFsData*>(data)

struct Esp32SdspiFsData {
    const std::string mount_path;
    const Esp32SdspiConfig* config;
    int spi_host;
    int cs_pin;
    sdmmc_card_t* card;

    Esp32SdspiFsData(const Esp32SdspiConfig* config, int spi_host, int cs_pin, const std::string& mount_path) :
        mount_path(mount_path),
        config(config),
        spi_host(spi_host),
        cs_pin(cs_pin),
        card(nullptr)
    {}
};

static gpio_num_t to_native_pin(GpioPinSpec pin_spec) {
    if (pin_spec.gpio_controller == nullptr) { return GPIO_NUM_NC; }
    return static_cast<gpio_num_t>(pin_spec.pin);
}

extern "C" {

Esp32SdspiHandle esp32_sdspi_fs_alloc(const Esp32SdspiConfig* config, int spi_host, int cs_pin, const char* mount_path) {
    return new(std::nothrow) Esp32SdspiFsData(config, spi_host, cs_pin, mount_path);
}

void esp32_sdspi_fs_free(Esp32SdspiHandle handle) {
    delete GET_DATA(handle);
}

sdmmc_card_t* esp32_sdspi_fs_get_card(Esp32SdspiHandle handle) {
    return GET_DATA(handle)->card;
}

static error_t mount(void* data) {
    auto* fs_data = GET_DATA(data);
    auto* config = fs_data->config;

    LOG_I(TAG, "Mounting %s", fs_data->mount_path.c_str());

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 0,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = static_cast<spi_host_device_t>(fs_data->spi_host);
    slot_config.gpio_cs = static_cast<gpio_num_t>(fs_data->cs_pin);
    slot_config.gpio_cd = to_native_pin(config->pin_cd);
    slot_config.gpio_wp = to_native_pin(config->pin_wp);
    slot_config.gpio_int = to_native_pin(config->pin_int);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = static_cast<int>(config->frequency_khz);
    host.slot = fs_data->spi_host;

    esp_err_t result = esp_vfs_fat_sdspi_mount(
        fs_data->mount_path.c_str(), &host, &slot_config, &mount_config, &fs_data->card
    );

    if (result != ESP_OK || fs_data->card == nullptr) {
        if (result == ESP_FAIL) {
            LOG_E(TAG, "Mounting failed. Ensure the card is formatted with FAT.");
        } else {
            LOG_E(TAG, "Mounting failed: %s", esp_err_to_name(result));
        }
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

const FileSystemApi esp32_sdspi_fs_api = {
    .mount = mount,
    .unmount = unmount,
    .is_mounted = is_mounted,
    .get_path = get_path
};

} // extern "C"
