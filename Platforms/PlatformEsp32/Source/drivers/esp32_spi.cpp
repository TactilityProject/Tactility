// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/esp32_spi.h>
#include <tactility/concurrent/mutex.h>

#include <esp_log.h>
#include <new>
#include <cstring>

#define TAG "esp32_spi"

#define GET_CONFIG(device) ((const struct Esp32SpiConfig*)device_get_config(device))
#define GET_DATA(device) ((struct InternalData*)device_get_driver_data(device))

extern "C" {

struct InternalData {
    Mutex mutex;
    bool initialized;

    InternalData() {
        mutex_construct(&mutex);
    }

    ~InternalData() {
        mutex_destruct(&mutex);
    }
};

static error_t reset(Device* device) {
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    if (driver_data->initialized) {
        spi_bus_free(dts_config->host);
        driver_data->initialized = false;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = dts_config->pin_mosi,
        .miso_io_num = dts_config->pin_miso,
        .sclk_io_num = dts_config->pin_sclk,
        .quadwp_io_num = dts_config->pin_wp,
        .quadhd_io_num = dts_config->pin_hd,
        .max_transfer_sz = dts_config->max_transfer_sz,
    };

    esp_err_t ret = spi_bus_initialize(dts_config->host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }

    driver_data->initialized = true;
    return ERROR_NONE;
}

static error_t lock(Device* device) {
    auto* driver_data = GET_DATA(device);
    mutex_lock(&driver_data->mutex);
    return ERROR_NONE;
}

static error_t try_lock(Device* device, TickType_t timeout) {
    auto* driver_data = GET_DATA(device);
    if (mutex_try_lock(&driver_data->mutex, timeout)) {
        return ERROR_NONE;
    }
    return ERROR_TIMEOUT;
}

static error_t unlock(Device* device) {
    auto* driver_data = GET_DATA(device);
    mutex_unlock(&driver_data->mutex);
    return ERROR_NONE;
}

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto* data = new(std::nothrow) InternalData();
    if (!data) return ERROR_OUT_OF_MEMORY;

    data->initialized = false;
    device_set_driver_data(device, data);

    return reset(device);
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    if (driver_data->initialized) {
        spi_bus_free(dts_config->host);
    }

    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

const static struct SpiControllerApi esp32_spi_api = {
    .reset = reset,
    .lock = lock,
    .try_lock = try_lock,
    .unlock = unlock
};

extern struct Module platform_module;

Driver esp32_spi_driver = {
    .name = "esp32_spi",
    .compatible = (const char*[]) { "espressif,esp32-spi", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = (void*)&esp32_spi_api,
    .device_type = &SPI_CONTROLLER_TYPE,
    .owner = &platform_module,
    .internal = nullptr
};

} // extern "C"
