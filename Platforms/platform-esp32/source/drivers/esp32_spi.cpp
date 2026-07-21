// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/log.h>

#include "tactility/drivers/gpio_descriptor.h"
#include <tactility/drivers/esp32_gpio_helpers.h>
#include <tactility/drivers/gpio_controller.h>
#include <cstring>
#include <new>
#include <soc/gpio_num.h>
#include <vector>

#define TAG "esp32_spi"

#define GET_CONFIG(device) ((const struct Esp32SpiConfig*)device->config)
#define GET_DATA(device) ((struct Esp32SpiInternal*)device_get_driver_data(device))

extern "C" {

struct Esp32SpiInternal {
    RecursiveMutex mutex = {};
    bool initialized = false;

    // Bus pin descriptors
    GpioDescriptor* sclk_descriptor = nullptr;
    GpioDescriptor* mosi_descriptor = nullptr;
    GpioDescriptor* miso_descriptor = nullptr;
    GpioDescriptor* wp_descriptor = nullptr;
    GpioDescriptor* hd_descriptor = nullptr;

    // CS pin descriptors
    std::vector<GpioDescriptor*> cs_descriptors;

    explicit Esp32SpiInternal() {
        recursive_mutex_construct(&mutex);
    }

    ~Esp32SpiInternal() {
        cleanup_pins();
        recursive_mutex_destruct(&mutex);
    }

    void cleanup_pins() {
        release_pin(&sclk_descriptor);
        release_pin(&mosi_descriptor);
        release_pin(&miso_descriptor);
        release_pin(&wp_descriptor);
        release_pin(&hd_descriptor);
        for (auto*& desc : cs_descriptors) {
            release_pin(&desc);
        }
        cs_descriptors.clear();
    }
};

static error_t lock(Device* device) {
    auto* driver_data = GET_DATA(device);
    recursive_mutex_lock(&driver_data->mutex);
    return ERROR_NONE;
}

static error_t try_lock(Device* device, TickType_t timeout) {
    auto* driver_data = GET_DATA(device);
    if (recursive_mutex_try_lock(&driver_data->mutex, timeout)) {
        return ERROR_NONE;
    }
    return ERROR_TIMEOUT;
}

static error_t unlock(Device* device) {
    auto* driver_data = GET_DATA(device);
    recursive_mutex_unlock(&driver_data->mutex);
    return ERROR_NONE;
}

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto* data = new (std::nothrow) Esp32SpiInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;

    device_set_driver_data(device, data);

    auto* dts_config = GET_CONFIG(device);

    // Acquire pins from the specified GPIO pin specs. Optional pins are allowed.
    bool pins_ok =
        acquire_pin_or_set_null(dts_config->pin_sclk, GPIO_FLAG_DIRECTION_OUTPUT, &data->sclk_descriptor) &&
        acquire_pin_or_set_null(dts_config->pin_mosi, GPIO_FLAG_DIRECTION_OUTPUT, &data->mosi_descriptor) &&
        acquire_pin_or_set_null(dts_config->pin_miso, GPIO_FLAG_DIRECTION_INPUT, &data->miso_descriptor) &&
        acquire_pin_or_set_null(dts_config->pin_wp, GPIO_FLAG_DIRECTION_INPUT, &data->wp_descriptor) &&
        acquire_pin_or_set_null(dts_config->pin_hd, GPIO_FLAG_DIRECTION_INPUT, &data->hd_descriptor);

    if (!pins_ok) {
        LOG_E(TAG, "Failed to acquire required SPI pins");
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        delete data;
        return ERROR_RESOURCE;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = get_native_pin(data->mosi_descriptor),
        .miso_io_num = get_native_pin(data->miso_descriptor),
        .sclk_io_num = get_native_pin(data->sclk_descriptor),
        .data2_io_num = get_native_pin(data->wp_descriptor),
        .data3_io_num = get_native_pin(data->hd_descriptor),
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .data_io_default_level = false,
        .max_transfer_sz = dts_config->max_transfer_size,
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0
    };

    esp_err_t ret = spi_bus_initialize(dts_config->host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        delete data;
        LOG_E(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }

    // MISO is only actively driven by the selected slave; between commands (and briefly during
    // slave selection/response) it floats, which can be read as spurious bits. A weak pull-up
    // costs nothing against an actively-driven line and avoids that, e.g. on SD-over-SPI this
    // shows up as CMD8/if_cond failing with a garbled/invalid response. Opt-in per board though:
    // it's been observed to instead prevent an SD card from responding to CMD0 at all elsewhere.
    if (data->miso_descriptor != nullptr && dts_config->miso_pull_up) {
        gpio_descriptor_set_flags(data->miso_descriptor, GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP);
    }

    // Acquire CS pins
    for (uint8_t i = 0; i < dts_config->cs_gpios_count; i++) {
        const GpioPinSpec* cs = &dts_config->cs_gpios[i];
        if (cs->gpio_controller == nullptr) continue;
        GpioDescriptor* desc = gpio_descriptor_acquire(cs->gpio_controller, cs->pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_SPI);
        if (desc != nullptr) {
            data->cs_descriptors.push_back(desc);
        }
    }

    esp32_spi_deselect_all_cs(device);

    data->initialized = true;
    return ERROR_NONE;
}

void esp32_spi_deselect_all_cs(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) return;
    for (auto* desc : data->cs_descriptors) {
        gpio_descriptor_set_level(desc, false);
    }
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    auto* driver_data = GET_DATA(device);
    auto* dts_config = GET_CONFIG(device);

    if (driver_data->initialized) {
        spi_bus_free(dts_config->host);
    }

    driver_data->cleanup_pins();
    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

error_t esp32_spi_get_cs_pin(Device* child_device, GpioPinSpec* out_pin) {
    auto* parent = device_get_parent(child_device);
    if (parent == nullptr || device_get_type(parent) != &SPI_CONTROLLER_TYPE) return ERROR_INVALID_STATE;
    auto* config = GET_CONFIG(parent);
    int32_t index = child_device->address;
    if (index < 0 || index >= config->cs_gpios_count) return ERROR_OUT_OF_RANGE;
    *out_pin = config->cs_gpios[index];
    return ERROR_NONE;
}

const static struct SpiControllerApi esp32_spi_api = {
    .lock = lock,
    .try_lock = try_lock,
    .unlock = unlock
};

extern struct Module platform_esp32_module;

Driver esp32_spi_driver = {
    .name = "esp32_spi",
    .compatible = (const char*[]) { "espressif,esp32-spi", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = (void*)&esp32_spi_api,
    .device_type = &SPI_CONTROLLER_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
