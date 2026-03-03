// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/esp32_sdmmc.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/log.h>

#include "tactility/drivers/gpio_descriptor.h"
#include <new>
#include <tactility/drivers/esp32_gpio_helpers.h>

#define TAG "esp32_sdmmc"

#define GET_CONFIG(device) ((const struct Esp32SdmmcConfig*)device->config)
#define GET_DATA(device) ((struct Esp32SdmmcInternal*)device_get_driver_data(device))

extern "C" {

struct Esp32SdmmcInternal {
    RecursiveMutex mutex = {};
    bool initialized = false;
    char fs_device_name[16] = "esp32_sdmmc_fs0";
    Device fs_device = {
        .name = fs_device_name,
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr
    };

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

    error_t try_lock(TickType_t timeout) { return recursive_mutex_try_lock(&mutex, timeout); }

    void unlock() { recursive_mutex_unlock(&mutex); }
};


static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    auto* data = new (std::nothrow) Esp32SdmmcInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;

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
        delete data;
        return ERROR_RESOURCE;
    }

    // Create filesystem child device
    auto* fs_device = &data->fs_device;
    fs_device->parent = device;
    fs_device->config = sdmmc_config;
    check(device_construct_add_start(fs_device, "espressif,esp32-sdmmc-fs") == ERROR_NONE);

    data->initialized = true;
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    // Create filesystem child device
    auto* fs_device = &data->fs_device;
    check(device_stop(fs_device) == ERROR_NONE);
    check(device_remove(fs_device) == ERROR_NONE);
    check(device_destruct(fs_device) == ERROR_NONE);

    data->cleanup_pins();
    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
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
