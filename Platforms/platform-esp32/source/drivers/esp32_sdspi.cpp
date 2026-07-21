// SPDX-License-Identifier: Apache-2.0
#include <new>

#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_gpio_helpers.h>
#include <tactility/drivers/esp32_sdspi.h>
#include <tactility/drivers/esp32_sdspi_fs.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/drivers/sdcard.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#define TAG "esp32_sdspi"

#define GET_CONFIG(device) ((const struct Esp32SdspiConfig*)device->config)
#define GET_DATA(device) ((struct Esp32SdspiInternal*)device_get_driver_data(device))

extern "C" {

struct Esp32SdspiInternal {
    RecursiveMutex mutex = {};
    Esp32SdspiHandle fs_handle = nullptr;
    FileSystem* file_system = nullptr;
    GpioDescriptor* pin_cd_descriptor = nullptr;
    GpioDescriptor* pin_wp_descriptor = nullptr;
    GpioDescriptor* pin_int_descriptor = nullptr;

    Esp32SdspiInternal() {
        recursive_mutex_construct(&mutex);
    }

    ~Esp32SdspiInternal() {
        cleanup_pins();
        recursive_mutex_destruct(&mutex);
        if (fs_handle) esp32_sdspi_fs_free(fs_handle);
    }

    void cleanup_pins() {
        release_pin(&pin_cd_descriptor);
        release_pin(&pin_wp_descriptor);
        release_pin(&pin_int_descriptor);
    }

    void lock() { recursive_mutex_lock(&mutex); }
    void unlock() { recursive_mutex_unlock(&mutex); }
};

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);

    auto* parent = device_get_parent(device);
    if (device_get_type(parent) != &SPI_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent device is not an SPI controller");
        return ERROR_INVALID_STATE;
    }

    auto* data = new (std::nothrow) Esp32SdspiInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;

    data->lock();
    device_set_driver_data(device, data);

    auto* config = GET_CONFIG(device);

    bool pins_ok =
        acquire_pin_or_set_null(config->pin_cd, &data->pin_cd_descriptor) &&
        acquire_pin_or_set_null(config->pin_wp, &data->pin_wp_descriptor) &&
        acquire_pin_or_set_null(config->pin_int, &data->pin_int_descriptor);

    if (!pins_ok) {
        LOG_E(TAG, "Failed to acquire one or more pins");
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        data->unlock();
        delete data;
        return ERROR_RESOURCE;
    }

    GpioPinSpec cs_pin_spec;
    if (esp32_spi_get_cs_pin(device, &cs_pin_spec) != ERROR_NONE) {
        LOG_E(TAG, "Failed to get CS pin from parent SPI controller");
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        data->unlock();
        delete data;
        return ERROR_RESOURCE;
    }

    auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);

    // Lower all CS pins
    esp32_spi_deselect_all_cs(parent);
    // Manually set the CS pin fo
    gpio_set_direction(static_cast<gpio_num_t>(cs_pin_spec.pin), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(cs_pin_spec.pin), 255);

    data->fs_handle = esp32_sdspi_fs_alloc(config, spi_config->host, cs_pin_spec.pin, "/sdcard");
    if (!data->fs_handle) {
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        data->unlock();
        delete data;
        return ERROR_OUT_OF_MEMORY;
    }

    data->file_system = file_system_add(&esp32_sdspi_fs_api, data->fs_handle);
    file_system_set_owner(data->file_system, device);
    if (file_system_mount(data->file_system) != ERROR_NONE) {
        LOG_E(TAG, "Failed to mount SD card filesystem");
    }

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

    file_system_remove(data->file_system);
    data->file_system = nullptr;

    esp32_sdspi_fs_free(data->fs_handle);
    data->fs_handle = nullptr;

    data->cleanup_pins();
    device_set_driver_data(device, nullptr);

    data->unlock();
    delete data;
    return ERROR_NONE;
}

sdmmc_card_t* esp32_sdspi_get_card(Device* device) {
    if (!device_is_ready(device)) return nullptr;
    auto* data = GET_DATA(device);
    if (!data) return nullptr;

    data->lock();
    auto* card = esp32_sdspi_fs_get_card(data->fs_handle);
    data->unlock();

    return card;
}

extern Module platform_esp32_module;

Driver esp32_sdspi_driver = {
    .name = "esp32_sdspi",
    .compatible = (const char*[]) { "espressif,esp32-sdspi", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = &SDCARD_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
