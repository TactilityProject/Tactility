// SPDX-License-Identifier: Apache-2.0
#include <driver/i2c.h>

#include <new>

#include <tactility/error_esp32.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/esp32_i2c.h>
#include <tactility/log.h>
#include <tactility/time.h>

#define TAG "esp32_i2c"
#define ACK_CHECK_EN 1

struct Esp32I2cInternal {
    Mutex mutex {};
    GpioDescriptor* sda_descriptor = nullptr;
    GpioDescriptor* scl_descriptor = nullptr;

    Esp32I2cInternal(GpioDescriptor* sda_descriptor, GpioDescriptor* scl_descriptor) :
        sda_descriptor(sda_descriptor),
        scl_descriptor(scl_descriptor)
    {
        mutex_construct(&mutex);
    }

    ~Esp32I2cInternal() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((Esp32I2cConfig*)device->config)
#define GET_DATA(device) ((Esp32I2cInternal*)device_get_driver_data(device))

#define lock(data) mutex_lock(&data->mutex);
#define unlock(data) mutex_unlock(&data->mutex);

extern "C" {

static error_t read(Device* device, uint8_t address, uint8_t* data, size_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_read_from_device(GET_CONFIG(device)->port, address, data, data_size, timeout);
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t write(Device* device, uint8_t address, const uint8_t* data, uint16_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_write_to_device(GET_CONFIG(device)->port, address, data, data_size, timeout);
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t write_read(Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (write_data_size == 0 || read_data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_write_read_device(GET_CONFIG(device)->port, address, write_data, write_data_size, read_data, read_data_size, timeout);
    unlock(driver_data);
    return esp_err_to_error(esp_error);
}

static error_t read_register(Device* device, uint8_t address, uint8_t reg, uint8_t* data, size_t data_size, TickType_t timeout) {
    auto start_time = get_ticks();
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);

    lock(driver_data);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t error = ESP_OK;
    if (cmd == nullptr) {
        error = ESP_ERR_NO_MEM;
        goto on_error;
    }
    // Set address pointer
    error = i2c_master_start(cmd);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_write(cmd, &reg, 1, ACK_CHECK_EN);
    if (error != ESP_OK) goto on_error;
    // Read length of response from current pointer
    error = i2c_master_start(cmd);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    if (error != ESP_OK) goto on_error;
    if (data_size > 1) {
        error = i2c_master_read(cmd, data, data_size - 1, I2C_MASTER_ACK);
        if (error != ESP_OK) goto on_error;
    }
    error = i2c_master_read_byte(cmd, data + data_size - 1, I2C_MASTER_NACK);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_stop(cmd);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_cmd_begin(GET_CONFIG(device)->port, cmd, get_timeout_remaining_ticks(timeout, start_time));
    if (error != ESP_OK) goto on_error;

    i2c_cmd_link_delete(cmd);
    unlock(driver_data);
    return esp_err_to_error(error);

on_error:
    i2c_cmd_link_delete(cmd);
    unlock(driver_data);
    return esp_err_to_error(error);
}

static error_t write_register(Device* device, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t data_size, TickType_t timeout) {
    auto start_time = get_ticks();
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);

    lock(driver_data);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t error = ESP_OK;
    if (cmd == nullptr) {
        error = ESP_ERR_NO_MEM;
        goto on_error;
    }
    error = i2c_master_start(cmd);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_write(cmd, (uint8_t*) data, data_size, ACK_CHECK_EN);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_stop(cmd);
    if (error != ESP_OK) goto on_error;
    error = i2c_master_cmd_begin(GET_CONFIG(device)->port, cmd, get_timeout_remaining_ticks(timeout, start_time));
    if (error != ESP_OK) goto on_error;

    i2c_cmd_link_delete(cmd);
    unlock(driver_data);
    return esp_err_to_error(error);

on_error:
    i2c_cmd_link_delete(cmd);
    unlock(driver_data);
    return esp_err_to_error(error);
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

    gpio_flags_t sda_flags, scl_flags;
    check(gpio_descriptor_get_flags(sda_descriptor, &sda_flags) == ERROR_NONE);
    check(gpio_descriptor_get_flags(scl_descriptor, &scl_flags) == ERROR_NONE);

    i2c_config_t esp_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .sda_pullup_en = (sda_flags & GPIO_FLAG_PULL_UP) != 0,
        .scl_pullup_en = (scl_flags & GPIO_FLAG_PULL_UP) != 0,
        .master {
            .clk_speed = dts_config->clockFrequency
        },
        .clk_flags = 0
    };

    esp_err_t error = i2c_param_config(dts_config->port, &esp_config);
    if (error != ESP_OK) {
        LOG_E(TAG, "Failed to configure port %d: %s", static_cast<int>(dts_config->port), esp_err_to_name(error));
        check(gpio_descriptor_release(sda_descriptor) == ERROR_NONE);
        check(gpio_descriptor_release(scl_descriptor) == ERROR_NONE);
        return ERROR_RESOURCE;
    }

    error = i2c_driver_install(dts_config->port, esp_config.mode, 0, 0, 0);
    if (error != ESP_OK) {
        LOG_E(TAG, "Failed to install driver at port %d: %s", static_cast<int>(dts_config->port), esp_err_to_name(error));
        check(gpio_descriptor_release(sda_descriptor) == ERROR_NONE);
        check(gpio_descriptor_release(scl_descriptor) == ERROR_NONE);
        return ERROR_RESOURCE;
    }

    auto* data = new(std::nothrow) Esp32I2cInternal(sda_descriptor, scl_descriptor);
    if (data == nullptr) {
        check(gpio_descriptor_release(sda_descriptor) == ERROR_NONE);
        check(gpio_descriptor_release(scl_descriptor) == ERROR_NONE);
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = static_cast<Esp32I2cInternal*>(device_get_driver_data(device));

    i2c_port_t port = GET_CONFIG(device)->port;
    esp_err_t result = i2c_driver_delete(port);
    if (result != ESP_OK) {
        LOG_E(TAG, "Failed to delete driver at port %d: %s", static_cast<int>(port), esp_err_to_name(result));
        return ERROR_RESOURCE;
    }

    check(gpio_descriptor_release(driver_data->sda_descriptor) == ERROR_NONE);
    check(gpio_descriptor_release(driver_data->scl_descriptor) == ERROR_NONE);

    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

static constexpr I2cControllerApi ESP32_I2C_API = {
    .read = read,
    .write = write,
    .write_read = write_read,
    .read_register = read_register,
    .write_register = write_register
};

extern Module platform_esp32_module;

Driver esp32_i2c_driver = {
    .name = "esp32_i2c",
    .compatible = (const char*[]) { "espressif,esp32-i2c", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &ESP32_I2C_API,
    .device_type = &I2C_CONTROLLER_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
