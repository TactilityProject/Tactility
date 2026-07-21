// SPDX-License-Identifier: Apache-2.0
#include <soc/soc_caps.h>
#if SOC_LCD_I80_SUPPORTED

#include <tactility/device.h>
#include <tactility/drivers/esp32_i8080.h>
#include <tactility/log.h>

#include "tactility/drivers/gpio_descriptor.h"
#include <tactility/drivers/esp32_gpio_helpers.h>
#include <tactility/drivers/gpio_controller.h>

#include <esp_lcd_panel_io.h>

#include <new>
#include <vector>

#define TAG "esp32_i8080"

#define GET_CONFIG(device) ((const struct Esp32I8080Config*)device->config)
#define GET_DATA(device) ((struct Esp32I8080Internal*)device_get_driver_data(device))

extern "C" {

struct Esp32I8080Internal {
    esp_lcd_i80_bus_handle_t bus_handle = nullptr;

    GpioDescriptor* dc_descriptor = nullptr;
    GpioDescriptor* wr_descriptor = nullptr;
    GpioDescriptor* rd_descriptor = nullptr;
    GpioDescriptor* d0_descriptor = nullptr;
    GpioDescriptor* d1_descriptor = nullptr;
    GpioDescriptor* d2_descriptor = nullptr;
    GpioDescriptor* d3_descriptor = nullptr;
    GpioDescriptor* d4_descriptor = nullptr;
    GpioDescriptor* d5_descriptor = nullptr;
    GpioDescriptor* d6_descriptor = nullptr;
    GpioDescriptor* d7_descriptor = nullptr;

    std::vector<GpioDescriptor*> cs_descriptors;

    void cleanup_pins() {
        release_pin(&dc_descriptor);
        release_pin(&wr_descriptor);
        release_pin(&rd_descriptor);
        release_pin(&d0_descriptor);
        release_pin(&d1_descriptor);
        release_pin(&d2_descriptor);
        release_pin(&d3_descriptor);
        release_pin(&d4_descriptor);
        release_pin(&d5_descriptor);
        release_pin(&d6_descriptor);
        release_pin(&d7_descriptor);
        for (auto*& desc : cs_descriptors) {
            release_pin(&desc);
        }
        cs_descriptors.clear();
    }
};

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    auto* data = new (std::nothrow) Esp32I8080Internal();
    if (data == nullptr) return ERROR_OUT_OF_MEMORY;

    device_set_driver_data(device, data);

    const auto* config = GET_CONFIG(device);

    bool pins_ok =
        acquire_pin_or_set_null(config->pin_dc, GPIO_FLAG_DIRECTION_OUTPUT, &data->dc_descriptor) &&
        acquire_pin_or_set_null(config->pin_wr, GPIO_FLAG_DIRECTION_OUTPUT, &data->wr_descriptor) &&
        acquire_pin_or_set_null(config->pin_rd, GPIO_FLAG_DIRECTION_OUTPUT, &data->rd_descriptor) &&
        acquire_pin_or_set_null(config->pin_d0, GPIO_FLAG_DIRECTION_OUTPUT, &data->d0_descriptor) &&
        acquire_pin_or_set_null(config->pin_d1, GPIO_FLAG_DIRECTION_OUTPUT, &data->d1_descriptor) &&
        acquire_pin_or_set_null(config->pin_d2, GPIO_FLAG_DIRECTION_OUTPUT, &data->d2_descriptor) &&
        acquire_pin_or_set_null(config->pin_d3, GPIO_FLAG_DIRECTION_OUTPUT, &data->d3_descriptor) &&
        acquire_pin_or_set_null(config->pin_d4, GPIO_FLAG_DIRECTION_OUTPUT, &data->d4_descriptor) &&
        acquire_pin_or_set_null(config->pin_d5, GPIO_FLAG_DIRECTION_OUTPUT, &data->d5_descriptor) &&
        acquire_pin_or_set_null(config->pin_d6, GPIO_FLAG_DIRECTION_OUTPUT, &data->d6_descriptor) &&
        acquire_pin_or_set_null(config->pin_d7, GPIO_FLAG_DIRECTION_OUTPUT, &data->d7_descriptor);

    if (!pins_ok) {
        LOG_E(TAG, "Failed to acquire required i8080 pins");
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        delete data;
        return ERROR_RESOURCE;
    }

    // RD is never toggled by the i80 LCD peripheral (write-only bus); drive it high once so the
    // panel doesn't see spurious read strobes.
    if (data->rd_descriptor != nullptr) {
        gpio_descriptor_set_level(data->rd_descriptor, true);
    }

    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = get_native_pin(data->dc_descriptor),
        .wr_gpio_num = get_native_pin(data->wr_descriptor),
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            get_native_pin(data->d0_descriptor),
            get_native_pin(data->d1_descriptor),
            get_native_pin(data->d2_descriptor),
            get_native_pin(data->d3_descriptor),
            get_native_pin(data->d4_descriptor),
            get_native_pin(data->d5_descriptor),
            get_native_pin(data->d6_descriptor),
            get_native_pin(data->d7_descriptor),
        },
        .bus_width = 8,
        .max_transfer_bytes = static_cast<size_t>(config->max_transfer_bytes),
        .psram_trans_align = 64,
        .sram_trans_align = 4
    };

    esp_err_t ret = esp_lcd_new_i80_bus(&bus_cfg, &data->bus_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create I80 bus: %s", esp_err_to_name(ret));
        data->cleanup_pins();
        device_set_driver_data(device, nullptr);
        delete data;
        return ERROR_RESOURCE;
    }

    // Acquire and deselect all CS pins
    for (uint8_t i = 0; i < config->cs_gpios_count; i++) {
        const GpioPinSpec* cs = &config->cs_gpios[i];
        if (cs->gpio_controller == nullptr) continue;
        GpioDescriptor* desc = gpio_descriptor_acquire(cs->gpio_controller, cs->pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
        if (desc != nullptr) {
            gpio_descriptor_set_level(desc, false); // logical low = physical high
            data->cs_descriptors.push_back(desc);
        } else {
            LOG_E(TAG, "Failed to acquire CS pin %u", i);
        }
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    auto* data = GET_DATA(device);

    if (data->bus_handle != nullptr) {
        esp_lcd_del_i80_bus(data->bus_handle);
    }

    data->cleanup_pins();
    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

error_t esp32_i8080_get_cs_pin(Device* child_device, GpioPinSpec* out_pin) {
    auto* parent = device_get_parent(child_device);
    if (parent == nullptr || device_get_type(parent) != &I8080_CONTROLLER_TYPE) return ERROR_INVALID_STATE;
    const auto* config = GET_CONFIG(parent);
    int32_t index = child_device->address;
    if (index < 0 || index >= config->cs_gpios_count) return ERROR_OUT_OF_RANGE;
    *out_pin = config->cs_gpios[index];
    return ERROR_NONE;
}

error_t esp32_i8080_get_bus_handle(Device* child_device, esp_lcd_i80_bus_handle_t* out_handle) {
    auto* parent = device_get_parent(child_device);
    if (parent == nullptr || device_get_type(parent) != &I8080_CONTROLLER_TYPE) return ERROR_INVALID_STATE;
    auto* data = GET_DATA(parent);
    if (data == nullptr || data->bus_handle == nullptr) return ERROR_INVALID_STATE;
    *out_handle = data->bus_handle;
    return ERROR_NONE;
}

extern struct Module platform_esp32_module;

Driver esp32_i8080_driver = {
    .name = "esp32_i8080",
    .compatible = (const char*[]) { "espressif,esp32-i8080", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = &I8080_CONTROLLER_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"

#endif // SOC_LCD_I80_SUPPORTED
