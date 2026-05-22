// SPDX-License-Identifier: Apache-2.0
#include <driver/i2c_master.h>

#include <new>
#include <cstring>

#include <tactility/error_esp32.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/esp32_i2c.h>
#include <tactility/log.h>
#include <tactility/time.h>
#include <tactility/check.h>

#define TAG "esp32_i2c"

struct Esp32I2cInternal {
    i2c_master_bus_handle_t bus_handle = nullptr;
    uint32_t clock_frequency_hz;
    Mutex mutex {};
    GpioDescriptor* sda_descriptor = nullptr;
    GpioDescriptor* scl_descriptor = nullptr;

    Esp32I2cInternal(GpioDescriptor* sda, GpioDescriptor* scl, uint32_t clk_hz)
        : clock_frequency_hz(clk_hz)
        , sda_descriptor(sda)
        , scl_descriptor(scl)
    {
        mutex_construct(&mutex);
    }

    ~Esp32I2cInternal() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((Esp32I2cConfig*)device->config)
#define GET_DATA(device) ((Esp32I2cInternal*)device_get_driver_data(device))
#define lock(data) mutex_lock(&data->mutex)
#define unlock(data) mutex_unlock(&data->mutex)

static inline int ticks_to_timeout_ms(TickType_t timeout) {
    if (timeout == portMAX_DELAY) return -1;
    int ms = (int)(timeout * portTICK_PERIOD_MS);
    return (ms < 1) ? 1 : ms;
}

extern "C" {

static error_t read(Device* device, uint8_t address, uint8_t* data, size_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = driver_data->clock_frequency_hz,
    };
    i2c_master_dev_handle_t dev_handle = nullptr;
    esp_err_t err = i2c_master_bus_add_device(driver_data->bus_handle, &dev_cfg, &dev_handle);
    if (err == ESP_OK) {
        err = i2c_master_receive(dev_handle, data, data_size, ticks_to_timeout_ms(timeout));
        i2c_master_bus_rm_device(dev_handle);
    }

    unlock(driver_data);
    return esp_err_to_error(err);
}

static error_t write(Device* device, uint8_t address, const uint8_t* data, uint16_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = driver_data->clock_frequency_hz,
    };
    i2c_master_dev_handle_t dev_handle = nullptr;
    esp_err_t err = i2c_master_bus_add_device(driver_data->bus_handle, &dev_cfg, &dev_handle);
    if (err == ESP_OK) {
        err = i2c_master_transmit(dev_handle, data, data_size, ticks_to_timeout_ms(timeout));
        i2c_master_bus_rm_device(dev_handle);
    }

    unlock(driver_data);
    return esp_err_to_error(err);
}

static error_t write_read(Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (write_data_size == 0 || read_data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = driver_data->clock_frequency_hz,
    };
    i2c_master_dev_handle_t dev_handle = nullptr;
    esp_err_t err = i2c_master_bus_add_device(driver_data->bus_handle, &dev_cfg, &dev_handle);
    if (err == ESP_OK) {
        err = i2c_master_transmit_receive(dev_handle, write_data, write_data_size, read_data, read_data_size, ticks_to_timeout_ms(timeout));
        i2c_master_bus_rm_device(dev_handle);
    }

    unlock(driver_data);
    return esp_err_to_error(err);
}

static error_t read_register(Device* device, uint8_t address, uint8_t reg, uint8_t* data, size_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = driver_data->clock_frequency_hz,
    };
    i2c_master_dev_handle_t dev_handle = nullptr;
    esp_err_t err = i2c_master_bus_add_device(driver_data->bus_handle, &dev_cfg, &dev_handle);
    if (err == ESP_OK) {
        err = i2c_master_transmit_receive(dev_handle, &reg, 1, data, data_size, ticks_to_timeout_ms(timeout));
        i2c_master_bus_rm_device(dev_handle);
    }

    unlock(driver_data);
    return esp_err_to_error(err);
}

static error_t write_register(Device* device, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t data_size, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    if (data_size == 0) return ERROR_INVALID_ARGUMENT;
    auto* driver_data = GET_DATA(device);
    lock(driver_data);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = driver_data->clock_frequency_hz,
    };
    i2c_master_dev_handle_t dev_handle = nullptr;
    esp_err_t err = i2c_master_bus_add_device(driver_data->bus_handle, &dev_cfg, &dev_handle);
    if (err == ESP_OK) {
        size_t total_size = 1 + data_size;
        if (total_size <= 256) {
            uint8_t buf[256];
            buf[0] = reg;
            std::memcpy(buf + 1, data, data_size);
            err = i2c_master_transmit(dev_handle, buf, total_size, ticks_to_timeout_ms(timeout));
        } else {
            auto* buf = (uint8_t*)std::malloc(total_size);
            if (buf) {
                buf[0] = reg;
                std::memcpy(buf + 1, data, data_size);
                err = i2c_master_transmit(dev_handle, buf, total_size, ticks_to_timeout_ms(timeout));
                std::free(buf);
            } else {
                err = ESP_ERR_NO_MEM;
            }
        }
        i2c_master_bus_rm_device(dev_handle);
    }

    unlock(driver_data);
    return esp_err_to_error(err);
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
        .flags = {
            .enable_internal_pullup = ((dts_config->pinSda.flags & GPIO_FLAG_PULL_UP) != 0) || ((dts_config->pinScl.flags & GPIO_FLAG_PULL_UP) != 0),
        },
    };

    i2c_master_bus_handle_t bus_handle = nullptr;
    esp_err_t error = i2c_new_master_bus(&bus_config, &bus_handle);
    if (error != ESP_OK) {
        LOG_E(TAG, "Failed to create I2C bus on port %d: %s", static_cast<int>(dts_config->port), esp_err_to_name(error));
        check(gpio_descriptor_release(sda_descriptor) == ERROR_NONE);
        check(gpio_descriptor_release(scl_descriptor) == ERROR_NONE);
        return esp_err_to_error(error);
    }

    auto* data = new(std::nothrow) Esp32I2cInternal(sda_descriptor, scl_descriptor, dts_config->clockFrequency);
    if (data == nullptr) {
        i2c_del_master_bus(bus_handle);
        check(gpio_descriptor_release(sda_descriptor) == ERROR_NONE);
        check(gpio_descriptor_release(scl_descriptor) == ERROR_NONE);
        return ERROR_OUT_OF_MEMORY;
    }

    data->bus_handle = bus_handle;
    device_set_driver_data(device, data);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = static_cast<Esp32I2cInternal*>(device_get_driver_data(device));

    esp_err_t result = i2c_del_master_bus(driver_data->bus_handle);
    if (result != ESP_OK) {
        LOG_E(TAG, "Failed to delete I2C bus: %s", esp_err_to_name(result));
        return esp_err_to_error(result);
    }

    check(gpio_descriptor_release(driver_data->sda_descriptor) == ERROR_NONE);
    check(gpio_descriptor_release(driver_data->scl_descriptor) == ERROR_NONE);

    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

i2c_master_bus_handle_t esp32_i2c_get_bus_handle(Device* device) {
    auto* driver_data = GET_DATA(device);
    if (driver_data == nullptr) return nullptr;
    return driver_data->bus_handle;
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
