// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_grove.h>
#include <tactility/drivers/esp32_i2c.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/drivers/esp32_uart.h>
#include <tactility/log.h>

#include <cstdio>
#include <cstring>
#include <new>

#define TAG "esp32_grove"

struct Esp32GroveInternal {
    Device* child_device = nullptr;
    void* child_config = nullptr;
    char* child_name = nullptr;
    GroveMode current_mode = GROVE_MODE_DISABLED;
};

#define GET_CONFIG(device) ((const struct Esp32GroveConfig*)device->config)
#define GET_DATA(device) ((struct Esp32GroveInternal*)device_get_driver_data(device))

extern "C" {

static error_t stop_child(Device* device) {
    auto* data = GET_DATA(device);
    if (!data) return ERROR_NONE;

    if (data->child_device) {
        if (data->child_device->internal) {
            if (device_is_added(data->child_device)) {
                if (device_is_ready(data->child_device)) {
                    if (device_stop(data->child_device) != ERROR_NONE) {
                        LOG_E(TAG, "%s: failed to stop child device", device->name);
                        return ERROR_RESOURCE;
                    }
                }
                if (device_remove(data->child_device) != ERROR_NONE) {
                    LOG_E(TAG, "%s: failed to remove child device", device->name);
                    return ERROR_RESOURCE;
                }
            }
            check(device_destruct(data->child_device) == ERROR_NONE);
        }
        delete data->child_device;
        data->child_device = nullptr;
    }

    if (data->child_config) {
        if (data->current_mode == GROVE_MODE_UART) {
            delete static_cast<Esp32UartConfig*>(data->child_config);
        } else if (data->current_mode == GROVE_MODE_I2C) {
            delete static_cast<Esp32I2cMasterConfig*>(data->child_config);
        }
        data->child_config = nullptr;
    }

    delete[] data->child_name;
    data->child_name = nullptr;

    data->current_mode = GROVE_MODE_DISABLED;
    return ERROR_NONE;
}

static error_t start_child(Device* device, GroveMode mode) {
    const auto* config = GET_CONFIG(device);
    auto* data = GET_DATA(device);

    if (mode == GROVE_MODE_DISABLED) {
        LOG_I(TAG, "%s: Grove port disabled", device->name);
        data->current_mode = GROVE_MODE_DISABLED;
        return ERROR_NONE;
    }

    data->child_device = new(std::nothrow) Device();
    if (!data->child_device) {
        return ERROR_OUT_OF_MEMORY;
    }
    std::memset(data->child_device, 0, sizeof(Device));

    size_t name_len = std::strlen(device->name) + 10;
    data->child_name = new(std::nothrow) char[name_len];
    if (!data->child_name) {
        delete data->child_device;
        data->child_device = nullptr;
        return ERROR_OUT_OF_MEMORY;
    }

    data->child_device->parent = device;
    const char* compatible = nullptr;

    if (mode == GROVE_MODE_UART) {
        // Device name
        std::snprintf(data->child_name, name_len, "%s_uart", device->name);
        data->child_device->name = data->child_name;
        // Device config
        auto* uart_cfg = new(std::nothrow) struct Esp32UartConfig();
        if (!uart_cfg) {
            delete[] data->child_name;
            data->child_name = nullptr;
            delete data->child_device;
            data->child_device = nullptr;
            return ERROR_OUT_OF_MEMORY;
        }
        std::memset(uart_cfg, 0, sizeof(Esp32UartConfig));
        uart_cfg->port = config->uartPort;
        uart_cfg->pin_tx = config->pinSclTx;
        uart_cfg->pin_rx = config->pinSdaRx;
        uart_cfg->pin_cts = GPIO_PIN_SPEC_NONE;
        uart_cfg->pin_rts = GPIO_PIN_SPEC_NONE;
        data->child_config = uart_cfg;
        compatible = "espressif,esp32-uart";
        LOG_I(TAG, "%s: starting UART mode on port %d", device->name, (int)uart_cfg->port);
    } else if (mode == GROVE_MODE_I2C) {
        // Device name
        std::snprintf(data->child_name, name_len, "%s_i2c", device->name);
        data->child_device->name = data->child_name;
        // Device config
        auto* i2c_cfg = new (std::nothrow) struct Esp32I2cMasterConfig();
        if (!i2c_cfg) {
            delete[] data->child_name;
            data->child_name = nullptr;
            delete data->child_device;
            data->child_device = nullptr;
            return ERROR_OUT_OF_MEMORY;
        }
        std::memset(i2c_cfg, 0, sizeof(Esp32I2cMasterConfig));
        i2c_cfg->port = static_cast<i2c_port_num_t>(config->i2cPort);
        i2c_cfg->clockFrequency = config->i2cClockFrequency;
        i2c_cfg->pinSda = config->pinSdaRx;
        i2c_cfg->pinScl = config->pinSclTx;
        i2c_cfg->clkSource = 0; // Default
        data->child_config = i2c_cfg;
        compatible = "espressif,esp32-i2c-master";
        LOG_I(TAG, "%s: starting I2C mode on port %d", device->name, (int)config->i2cPort);
    } else {
        LOG_E(TAG, "%s: unknown mode %d", device->name, mode);
        if (data->child_name != nullptr) {
            delete[] data->child_name;
            data->child_name = nullptr;
        }
        delete data->child_device;
        data->child_device = nullptr;
        return ERROR_INVALID_ARGUMENT;
    }

    data->child_device->config = data->child_config;

    error_t err = device_construct_add_start(data->child_device, compatible);
    if (err != ERROR_NONE) {
        LOG_E(TAG, "%s: failed to start child device: %d", device->name, err);
        stop_child(device);
        return err;
    }

    data->current_mode = mode;
    return ERROR_NONE;
}

static error_t start_device(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* data = new(std::nothrow) Esp32GroveInternal();
    if (!data) return ERROR_OUT_OF_MEMORY;
    device_set_driver_data(device, data);

    return start_child(device, config->defaultMode);
}

static error_t stop_device(Device* device) {
    auto* data = GET_DATA(device);
    if (!data) return ERROR_NONE;

    stop_child(device);
    delete data;
    device_set_driver_data(device, nullptr);

    return ERROR_NONE;
}

static error_t esp32_grove_set_mode(Device* device, enum GroveMode mode) {
    auto* data = GET_DATA(device);
    if (data->current_mode == mode) return ERROR_NONE;

    error_t err = stop_child(device);
    if (err != ERROR_NONE) return err;

    return start_child(device, mode);
}

static error_t esp32_grove_get_mode(Device* device, enum GroveMode* mode) {
    auto* data = GET_DATA(device);
    *mode = data->current_mode;
    return ERROR_NONE;
}

static const GroveApi esp32_grove_api = {
    .set_mode = esp32_grove_set_mode,
    .get_mode = esp32_grove_get_mode
};

extern Module platform_esp32_module;

Driver esp32_grove_driver = {
    .name = "esp32_grove",
    .compatible = (const char*[]) { "espressif,esp32-grove", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .api = &esp32_grove_api,
    .device_type = &GROVE_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
