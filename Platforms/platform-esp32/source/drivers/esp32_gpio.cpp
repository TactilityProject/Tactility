// SPDX-License-Identifier: Apache-2.0
#include <driver/gpio.h>

#include <tactility/driver.h>
#include <tactility/drivers/esp32_gpio.h>

#include <tactility/error_esp32.h>
#include <tactility/log.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>

#define TAG "esp32_gpio"

struct Esp32GpioInternal {
    uint8_t isr_service_ref_count = 0;
};

#define GET_CONFIG(device) ((struct Esp32GpioConfig*)device->config)
#define GET_INTERNAL_FROM_DESCRIPTOR(gpio_descriptor) ((struct Esp32GpioInternal*)gpio_descriptor->controller_context)

extern "C" {

static error_t set_level(GpioDescriptor* descriptor, bool high) {
    auto esp_error = gpio_set_level(static_cast<gpio_num_t>(descriptor->pin), high);
    return esp_err_to_error(esp_error);
}

static error_t get_level(GpioDescriptor* descriptor, bool* high) {
    *high = gpio_get_level(static_cast<gpio_num_t>(descriptor->pin)) != 0;
    return ERROR_NONE;
}

static error_t set_flags(GpioDescriptor* descriptor, gpio_flags_t flags) {
    const Esp32GpioConfig* config = GET_CONFIG(descriptor->controller);

    if (descriptor->pin >= config->gpioCount) {
        return ERROR_INVALID_ARGUMENT;
    }

    gpio_mode_t mode;
    if ((flags & GPIO_FLAG_DIRECTION_INPUT_OUTPUT) == GPIO_FLAG_DIRECTION_INPUT_OUTPUT) {
        mode = GPIO_MODE_INPUT_OUTPUT;
    } else if (flags & GPIO_FLAG_DIRECTION_INPUT) {
        mode = GPIO_MODE_INPUT;
    } else if (flags & GPIO_FLAG_DIRECTION_OUTPUT) {
        mode = GPIO_MODE_OUTPUT;
    } else {
        return ERROR_INVALID_ARGUMENT;
    }

    const gpio_config_t esp_config = {
        .pin_bit_mask = 1ULL << descriptor->pin,
        .mode = mode,
        .pull_up_en = (flags & GPIO_FLAG_PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (flags & GPIO_FLAG_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_FLAG_INTERRUPT_FROM_OPTIONS(flags),
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE
#endif
    };

    auto esp_error = gpio_config(&esp_config);
    return esp_err_to_error(esp_error);
}

static error_t get_flags(GpioDescriptor* descriptor, gpio_flags_t* flags) {
    gpio_io_config_t esp_config;
    if (gpio_get_io_config(static_cast<gpio_num_t>(descriptor->pin), &esp_config) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    gpio_flags_t output = 0;

    if (esp_config.pu) {
        output |= GPIO_FLAG_PULL_UP;
    }

    if (esp_config.pd) {
        output |= GPIO_FLAG_PULL_DOWN;
    }

    if (esp_config.ie) {
        output |= GPIO_FLAG_DIRECTION_INPUT;
    }

    if (esp_config.oe) {
        output |= GPIO_FLAG_DIRECTION_OUTPUT;
    }

    if (esp_config.oe_inv) {
        output |= GPIO_FLAG_ACTIVE_LOW;
    }

    *flags = output;
    return ERROR_NONE;
}

static error_t get_native_pin_number(GpioDescriptor* descriptor, void* pin_number) {
    auto* esp_pin_number = reinterpret_cast<gpio_num_t*>(pin_number);
    *esp_pin_number = static_cast<gpio_num_t>(descriptor->pin);
    return ERROR_NONE;
}

static error_t add_callback(GpioDescriptor* descriptor, void (*callback)(void*), void* arg) {
    auto* internal = GET_INTERNAL_FROM_DESCRIPTOR(descriptor);
    if (internal->isr_service_ref_count == 0) {
        auto esp_error = gpio_install_isr_service(0);
        if (esp_error != ESP_OK && esp_error != ESP_ERR_INVALID_STATE) {
            return esp_err_to_error(esp_error);
        }
    }

    auto esp_error = gpio_isr_handler_add(static_cast<gpio_num_t>(descriptor->pin), callback, arg);

    if (esp_error == ESP_OK) {
        internal->isr_service_ref_count++;
    } else if (internal->isr_service_ref_count == 0) {
        gpio_uninstall_isr_service();
    }

    return esp_err_to_error(esp_error);
}

static error_t remove_callback(GpioDescriptor* descriptor) {
    auto esp_error = gpio_isr_handler_remove(static_cast<gpio_num_t>(descriptor->pin));
    if (esp_error == ESP_OK) {
        auto* internal = GET_INTERNAL_FROM_DESCRIPTOR(descriptor);
        check(internal->isr_service_ref_count > 0);
        internal->isr_service_ref_count--;
        if (internal->isr_service_ref_count == 0) {
            gpio_uninstall_isr_service();
        }
    }
    return esp_err_to_error(esp_error);
}

static error_t enable_interrupt(GpioDescriptor* descriptor) {
    auto esp_error = gpio_intr_enable(static_cast<gpio_num_t>(descriptor->pin));
    return esp_err_to_error(esp_error);
}

static error_t disable_interrupt(GpioDescriptor* descriptor) {
    auto esp_error = gpio_intr_disable(static_cast<gpio_num_t>(descriptor->pin));
    return esp_err_to_error(esp_error);
}

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    const Esp32GpioConfig* config = GET_CONFIG(device);
    auto* internal = new Esp32GpioInternal();
    return gpio_controller_init_descriptors(device, config->gpioCount, internal);
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    const Esp32GpioConfig* config = GET_CONFIG(device);
    auto* internal = static_cast<Esp32GpioInternal*>(gpio_controller_get_controller_context(device));

    // First, remove all ISR handlers to prevent callbacks during cleanup
    for (uint8_t i = 0; i < config->gpioCount; ++i) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(i));
    }

    // Then uninstall ISR service
    if (internal->isr_service_ref_count > 0) {
        gpio_uninstall_isr_service();
    }

    // Now safe to deinit descriptors and delete internal
    check(gpio_controller_deinit_descriptors(device) == ERROR_NONE);
    delete internal;

    return ERROR_NONE;
}

const static GpioControllerApi esp32_gpio_api  = {
    .set_level = set_level,
    .get_level = get_level,
    .set_flags = set_flags,
    .get_flags = get_flags,
    .get_native_pin_number = get_native_pin_number,
    .add_callback = add_callback,
    .remove_callback = remove_callback,
    .enable_interrupt = enable_interrupt,
    .disable_interrupt = disable_interrupt
};

extern Module platform_esp32_module;

Driver esp32_gpio_driver = {
    .name = "esp32_gpio",
    .compatible = (const char*[]) { "espressif,esp32-gpio", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = static_cast<const void*>(&esp32_gpio_api),
    .device_type = &GPIO_CONTROLLER_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
