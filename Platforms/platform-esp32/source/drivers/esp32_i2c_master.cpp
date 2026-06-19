// SPDX-License-Identifier: Apache-2.0
#include <driver/i2c_master.h>

#include <new>

#include <tactility/error_esp32.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/log.h>
#include <tactility/time.h>

#define TAG "esp32_i2c_master"

struct Esp32I2cMasterInternal {
    Mutex mutex {};
    GpioDescriptor* sda_descriptor = nullptr;
    GpioDescriptor* scl_descriptor = nullptr;
    i2c_master_bus_handle_t bus_handle = nullptr;
    i2c_master_dev_handle_t dev_handle = nullptr;
    int current_address = -1;

    Esp32I2cMasterInternal(GpioDescriptor* sda_descriptor, GpioDescriptor* scl_descriptor) :
        sda_descriptor(sda_descriptor),
        scl_descriptor(scl_descriptor)
    {
        mutex_construct(&mutex);
    }

    ~Esp32I2cMasterInternal() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((Esp32I2cMasterConfig*)device->config)
#define GET_DATA(device) ((Esp32I2cMasterInternal*)device_get_driver_data(device))

#define lock(data) mutex_lock(&data->mutex);
#define unlock(data) mutex_unlock(&data->mutex);

// Switches the device's target address only when it differs from the currently configured one.
static esp_err_t ensure_address(Esp32I2cMasterInternal* driver_data, uint8_t address, int timeout_ms) {
    if (driver_data->current_address == address) {
        return ESP_OK;
    }
    esp_err_t esp_error = i2c_master_device_change_address(driver_data->dev_handle, address, timeout_ms);
    if (esp_error == ESP_OK) {
        driver_data->current_address = address;
    }
    return esp_error;
}

extern "C" {

static int ticks_to_ms(TickType_t ticks) {
    if (ticks == portMAX_DELAY) return -1;
    return (int)pdTICKS_TO_MS(ticks);
}

static error_t read(Device* device, uint8_t address, uint8_t* data, size_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    int timeout_ms = ticks_to_ms(timeout);

    lock(driver_data);
    esp_err_t esp_error = ensure_address(driver_data, address, timeout_ms);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "change_address(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
    } else {
        esp_error = i2c_master_receive(driver_data->dev_handle, data, data_size, timeout_ms);
        if (esp_error != ESP_OK) {
            LOG_E(TAG, "receive(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
        }
    }
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t write(Device* device, uint8_t address, const uint8_t* data, uint16_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    int timeout_ms = ticks_to_ms(timeout);

    lock(driver_data);
    esp_err_t esp_error = ensure_address(driver_data, address, timeout_ms);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "change_address(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
    } else {
        esp_error = i2c_master_transmit(driver_data->dev_handle, data, data_size, timeout_ms);
        if (esp_error != ESP_OK) {
            LOG_E(TAG, "transmit(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
        }
    }
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t write_read(Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (write_data_size == 0 || read_data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    int timeout_ms = ticks_to_ms(timeout);

    lock(driver_data);
    esp_err_t esp_error = ensure_address(driver_data, address, timeout_ms);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "change_address(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
    } else {
        esp_error = i2c_master_transmit_receive(driver_data->dev_handle, write_data, write_data_size, read_data, read_data_size, timeout_ms);
        if (esp_error != ESP_OK) {
            LOG_E(TAG, "transmit_receive(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
        }
    }
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t read_register(Device* device, uint8_t address, uint8_t reg, uint8_t* data, size_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    int timeout_ms = ticks_to_ms(timeout);

    lock(driver_data);
    esp_err_t esp_error = ensure_address(driver_data, address, timeout_ms);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "change_address(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
    } else {
        esp_error = i2c_master_transmit_receive(driver_data->dev_handle, &reg, 1, data, data_size, timeout_ms);
        if (esp_error != ESP_OK) {
            LOG_E(TAG, "read_register(0x%02X, reg=0x%02X) failed: %s", address, reg, esp_err_to_name(esp_error));
        }
    }
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t write_register(Device* device, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    int timeout_ms = ticks_to_ms(timeout);

    lock(driver_data);
    esp_err_t esp_error = ensure_address(driver_data, address, timeout_ms);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "change_address(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
    } else {
        i2c_master_transmit_multi_buffer_info_t buffers[2] = {
            {.write_buffer = &reg, .buffer_size = 1},
            {.write_buffer = data, .buffer_size = data_size}
        };
        esp_error = i2c_master_multi_buffer_transmit(driver_data->dev_handle, buffers, 2, timeout_ms);
        if (esp_error != ESP_OK) {
            LOG_E(TAG, "write_register(0x%02X, reg=0x%02X) failed: %s", address, reg, esp_err_to_name(esp_error));
        }
    }
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t probe(Device* device, uint8_t address, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* driver_data = GET_DATA(device);
    int timeout_ms = ticks_to_ms(timeout);

    lock(driver_data);
    esp_err_t esp_error = i2c_master_probe(driver_data->bus_handle, address, timeout_ms);
    if (esp_error == ESP_ERR_NOT_FOUND) {
        // Expected outcome when no device acks - e.g. hot-plug attach polling
        LOG_D(TAG, "probe(0x%02X): not found", address);
    } else if (esp_error != ESP_OK) {
        LOG_E(TAG, "probe(0x%02X) failed: %s", address, esp_err_to_name(esp_error));
    }
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto dts_config = GET_CONFIG(device);

    auto& sda_spec = dts_config->pinSda;
    auto& scl_spec = dts_config->pinScl;
    auto* sda_descriptor = gpio_descriptor_acquire(sda_spec.gpio_controller, sda_spec.pin, GPIO_OWNER_GPIO);
    if (!sda_descriptor) {
        LOG_E(TAG, "Failed to acquire pin %u", sda_spec.pin);
        return ERROR_RESOURCE;
    }

    auto* scl_descriptor = gpio_descriptor_acquire(scl_spec.gpio_controller, scl_spec.pin, GPIO_OWNER_GPIO);
    if (!scl_descriptor) {
        LOG_E(TAG, "Failed to acquire pin %u", scl_spec.pin);
        gpio_descriptor_release(sda_descriptor);
        return ERROR_RESOURCE;
    }

    gpio_num_t sda_pin, scl_pin;
    check(gpio_descriptor_get_native_pin_number(sda_descriptor, &sda_pin) == ERROR_NONE);
    check(gpio_descriptor_get_native_pin_number(scl_descriptor, &scl_pin) == ERROR_NONE);

    i2c_master_bus_config_t bus_config = {
        .i2c_port = dts_config->port,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = ((sda_spec.flags & GPIO_FLAG_PULL_UP) != 0) || ((scl_spec.flags & GPIO_FLAG_PULL_UP) != 0),
            .allow_pd = 0,
        }
    };

#if SOC_LP_I2C_SUPPORTED
    if (dts_config->port == LP_I2C_NUM_0) {
        bus_config.lp_source_clk = (dts_config->clkSource == 0) ? LP_I2C_SCLK_DEFAULT : static_cast<lp_i2c_clock_source_t>(dts_config->clkSource);
    } else {
        bus_config.clk_source = (dts_config->clkSource == 0) ? I2C_CLK_SRC_DEFAULT : static_cast<i2c_clock_source_t>(dts_config->clkSource);
    }
#else
    bus_config.clk_source = (dts_config->clkSource == 0) ? I2C_CLK_SRC_DEFAULT : static_cast<i2c_clock_source_t>(dts_config->clkSource);
#endif

    i2c_master_bus_handle_t bus_handle;
    esp_err_t error = i2c_new_master_bus(&bus_config, &bus_handle);
    if (error != ESP_OK) {
        LOG_E(TAG, "Failed to create I2C bus at port %d: %s", (int)dts_config->port, esp_err_to_name(error));
        gpio_descriptor_release(sda_descriptor);
        gpio_descriptor_release(scl_descriptor);
        return ERROR_RESOURCE;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0, // Will be changed at runtime
        .scl_speed_hz = dts_config->clockFrequency,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        }
    };

    i2c_master_dev_handle_t dev_handle;
    error = i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle);
    if (error != ESP_OK) {
        LOG_E(TAG, "Failed to add I2C device: %s", esp_err_to_name(error));
        i2c_del_master_bus(bus_handle);
        gpio_descriptor_release(sda_descriptor);
        gpio_descriptor_release(scl_descriptor);
        return ERROR_RESOURCE;
    }

    auto* data = new(std::nothrow) Esp32I2cMasterInternal(sda_descriptor, scl_descriptor);
    if (data == nullptr) {
        i2c_master_bus_rm_device(dev_handle);
        i2c_del_master_bus(bus_handle);
        gpio_descriptor_release(sda_descriptor);
        gpio_descriptor_release(scl_descriptor);
        return ERROR_OUT_OF_MEMORY;
    }

    data->bus_handle = bus_handle;
    data->dev_handle = dev_handle;

    device_set_driver_data(device, data);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = GET_DATA(device);

    i2c_master_bus_rm_device(driver_data->dev_handle);
    i2c_del_master_bus(driver_data->bus_handle);

    gpio_descriptor_release(driver_data->sda_descriptor);
    gpio_descriptor_release(driver_data->scl_descriptor);

    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

i2c_master_bus_handle_t esp32_i2c_master_get_bus_handle(struct Device* device) {
    return GET_DATA(device)->bus_handle;
}

uint32_t esp32_i2c_master_get_clock_frequency(struct Device* device) {
    return GET_CONFIG(device)->clockFrequency;
}

static constexpr I2cControllerApi ESP32_I2C_MASTER_API = {
    .read = read,
    .write = write,
    .write_read = write_read,
    .read_register = read_register,
    .write_register = write_register,
    .probe = probe
};

extern Module platform_esp32_module;

Driver esp32_i2c_master_driver = {
    .name = "esp32_i2c_master",
    .compatible = (const char*[]) { "espressif,esp32-i2c-master", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &ESP32_I2C_MASTER_API,
    .device_type = &I2C_CONTROLLER_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
