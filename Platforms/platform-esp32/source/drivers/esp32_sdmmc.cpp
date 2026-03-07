// SPDX-License-Identifier: Apache-2.0
#include <soc/soc_caps.h>
#if SOC_SDMMC_HOST_SUPPORTED

#include <new>

#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/device.h>
#include <tactility/drivers/esp32_gpio_helpers.h>
#include <tactility/drivers/esp32_sdmmc.h>
#include <tactility/drivers/esp32_sdmmc_fs.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#define TAG "esp32_sdmmc"

#define GET_CONFIG(device) ((const struct Esp32SdmmcConfig*)device->config)
#define GET_DATA(device) ((struct Esp32SdmmcInternal*)device_get_driver_data(device))

extern "C" {

struct Esp32SdmmcInternal {
    RecursiveMutex mutex = {};
    bool initialized = false;
    Esp32SdmmcHandle esp32_sdmmc_fs_handle = nullptr;
    FileSystem* file_system = nullptr;

    // Pin descriptors
    GpioDescriptor* pin_clk_descriptor = nullptr;
    GpioDescriptor* pin_cmd_descriptor = nullptr;
    GpioDescriptor* pin_d0_descriptor = nullptr;
    GpioDescriptor* pin_d1_descriptor = nullptr;
    GpioDescriptor* pin_d2_descriptor = nullptr;
    GpioDescriptor* pin_d3_descriptor = nullptr;
    GpioDescriptor* pin_d4_descriptor = nullptr;
    GpioDescriptor* pin_d5_descriptor = nullptr;
    GpioDescriptor* pin_d6_descriptor = nullptr;
    GpioDescriptor* pin_d7_descriptor = nullptr;
    GpioDescriptor* pin_cd_descriptor = nullptr;
    GpioDescriptor* pin_wp_descriptor = nullptr;

    explicit Esp32SdmmcInternal() {
        recursive_mutex_construct(&mutex);
    }

    ~Esp32SdmmcInternal() {
        cleanup_pins();
        recursive_mutex_destruct(&mutex);
        if (esp32_sdmmc_fs_handle) esp32_sdmmc_fs_free(esp32_sdmmc_fs_handle);
    }

    void cleanup_pins() {
        release_pin(&pin_clk_descriptor);
        release_pin(&pin_cmd_descriptor);
        release_pin(&pin_d0_descriptor);
        release_pin(&pin_d1_descriptor);
        release_pin(&pin_d2_descriptor);
        release_pin(&pin_d3_descriptor);
        release_pin(&pin_d4_descriptor);
        release_pin(&pin_d5_descriptor);
        release_pin(&pin_d6_descriptor);
        release_pin(&pin_d7_descriptor);
        release_pin(&pin_cd_descriptor);
        release_pin(&pin_wp_descriptor);
    }

    void lock() { recursive_mutex_lock(&mutex); }

    void unlock() { recursive_mutex_unlock(&mutex); }
};


static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    auto* data = new (std::nothrow) Esp32SdmmcInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;

    data->lock();
    device_set_driver_data(device, data);

    auto* sdmmc_config = GET_CONFIG(device);

    // Acquire pins from the specified GPIO pin specs. Optional pins are allowed.
    bool pins_ok =
        acquire_pin_or_set_null(sdmmc_config->pin_clk, &data->pin_clk_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_cmd, &data->pin_cmd_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_d0, &data->pin_d0_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_d1, &data->pin_d1_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_d2, &data->pin_d2_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_d3, &data->pin_d3_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_d4, &data->pin_d4_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_d5, &data->pin_d5_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_d6, &data->pin_d6_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_d7, &data->pin_d7_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_cd, &data->pin_cd_descriptor) &&
        acquire_pin_or_set_null(sdmmc_config->pin_wp, &data->pin_wp_descriptor);

    if (!pins_ok) {
        LOG_E(TAG, "Failed to acquire required one or more pins");
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        data->unlock();
        delete data;
        return ERROR_RESOURCE;
    }

    data->esp32_sdmmc_fs_handle = esp32_sdmmc_fs_alloc(sdmmc_config, "/sdcard");
    if (!data->esp32_sdmmc_fs_handle) {
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        data->unlock();
        delete data;
        return ERROR_OUT_OF_MEMORY;
    }

    data->file_system = file_system_add(&esp32_sdmmc_fs_api, data->esp32_sdmmc_fs_handle);
    if (file_system_mount(data->file_system) != ERROR_NONE) {
        // Error is not recoverable at the time, but it might be recoverable later,
        // so we don't return start() failure.
        LOG_E(TAG, "Failed to mount SD card filesystem");
    }

    data->initialized = true;
    data->unlock();
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    auto* data = GET_DATA(device);
    if (!data) return ERROR_NONE;

    data->lock();

    if (file_system_is_mounted(data->file_system)) {
        if (file_system_unmount(data->file_system) != ERROR_NONE) {
            LOG_E(TAG, "Failed to unmount SD card filesystem");
            data->unlock();
            return ERROR_RESOURCE;
        }
    }

    // Free file system
    file_system_remove(data->file_system);
    data->file_system = nullptr;
    // Free file system data
    esp32_sdmmc_fs_free(data->esp32_sdmmc_fs_handle);
    data->esp32_sdmmc_fs_handle = nullptr;

    data->cleanup_pins();
    device_set_driver_data(device, nullptr);

    data->unlock();
    delete data;
    return ERROR_NONE;
}

sdmmc_card_t* esp32_sdmmc_get_card(Device* device) {
    if (!device_is_ready(device)) return nullptr;
    auto* data = GET_DATA(device);
    if (!data) return nullptr;

    data->lock();
    auto* card = esp32_sdmmc_fs_get_card(data->esp32_sdmmc_fs_handle);
    data->unlock();

    return card;
}

extern Module platform_esp32_module;

Driver esp32_sdmmc_driver = {
    .name = "esp32_sdmmc",
    .compatible = (const char*[]) { "espressif,esp32-sdmmc", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
#endif