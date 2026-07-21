// SPDX-License-Identifier: Apache-2.0
#include <drivers/ch422g.h>
#include <ch422g_module.h>
#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <new>

constexpr auto* TAG = "CH422G";

#define GET_CONFIG(device) (static_cast<const Ch422gConfig*>((device)->config))

// The chip has no address-select pins: RD_IO and WR_IO are always at these fixed
// pseudo-addresses, regardless of what's configured as the WR_SET (system config) address.
constexpr uint8_t CH422G_ADDR_RD_IO = 0x26;
constexpr uint8_t CH422G_ADDR_WR_IO = 0x38;

// WR_SET bit that switches EXIO0-7 into GPIO output mode.
constexpr uint8_t CH422G_SYSTEM_CONFIG_IO_OE = 0x01;

struct Ch422gContext {
    Mutex mutex {};
    // WR_IO has no readback, so the last written value is tracked here for read-modify-write.
    uint8_t output_shadow = 0x00;
};

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    if (device_get_type(parent) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent device is not I2C");
        return ERROR_RESOURCE;
    }

    uint8_t wr_set = CH422G_SYSTEM_CONFIG_IO_OE;
    if (i2c_controller_write(parent, GET_CONFIG(device)->address, &wr_set, 1, portMAX_DELAY) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure system register");
        return ERROR_RESOURCE;
    }

    auto* context = new(std::nothrow) Ch422gContext();
    if (context == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    mutex_construct(&context->mutex);

    // Drive all outputs low, matching the shadow's initial value.
    if (i2c_controller_write(parent, CH422G_ADDR_WR_IO, &context->output_shadow, 1, portMAX_DELAY) != ERROR_NONE) {
        LOG_W(TAG, "Failed to initialize outputs (non-fatal)");
    }

    error_t error = gpio_controller_init_descriptors(device, 8, context);
    if (error != ERROR_NONE) {
        mutex_destruct(&context->mutex);
        delete context;
        return error;
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* context = static_cast<Ch422gContext*>(gpio_controller_get_controller_context(device));
    check(gpio_controller_deinit_descriptors(device) == ERROR_NONE);
    mutex_destruct(&context->mutex);
    delete context;
    return ERROR_NONE;
}

extern "C" {

static error_t set_level(GpioDescriptor* descriptor, bool high) {
    auto* context = static_cast<Ch422gContext*>(descriptor->controller_context);
    auto* parent = device_get_parent(descriptor->controller);
    uint8_t bit = 1 << descriptor->pin;

    mutex_lock(&context->mutex);
    uint8_t new_shadow = high ? (context->output_shadow | bit) : static_cast<uint8_t>(context->output_shadow & ~bit);
    error_t err = i2c_controller_write(parent, CH422G_ADDR_WR_IO, &new_shadow, 1, portMAX_DELAY);
    if (err == ERROR_NONE) {
        context->output_shadow = new_shadow;
    }
    mutex_unlock(&context->mutex);

    return err;
}

static error_t get_level(GpioDescriptor* descriptor, bool* high) {
    auto* parent = device_get_parent(descriptor->controller);
    uint8_t bits;

    error_t err = i2c_controller_read(parent, CH422G_ADDR_RD_IO, &bits, 1, portMAX_DELAY);
    if (err != ERROR_NONE) {
        return err;
    }

    *high = (bits & (1 << descriptor->pin)) != 0;
    return ERROR_NONE;
}

static error_t set_flags(GpioDescriptor*, gpio_flags_t flags) {
    // The chip has no per-pin direction register: EXIO0-7 are outputs once WR_SET enables
    // IO_OE. Pull-up/down and high-impedance (true input mode) are not present either.
    if (flags & (GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP | GPIO_FLAG_PULL_DOWN | GPIO_FLAG_HIGH_IMPEDANCE)) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NONE;
}

static error_t get_flags(GpioDescriptor*, gpio_flags_t* flags) {
    *flags = GPIO_FLAG_DIRECTION_OUTPUT;
    return ERROR_NONE;
}

static error_t get_native_pin_number(GpioDescriptor*, void*) {
    return ERROR_NOT_SUPPORTED;
}

static error_t add_callback(GpioDescriptor*, void (*)(void*), void*) {
    return ERROR_NOT_SUPPORTED;
}

static error_t remove_callback(GpioDescriptor*) {
    return ERROR_NOT_SUPPORTED;
}

static error_t enable_interrupt(GpioDescriptor*) {
    return ERROR_NOT_SUPPORTED;
}

static error_t disable_interrupt(GpioDescriptor*) {
    return ERROR_NOT_SUPPORTED;
}

const static GpioControllerApi ch422g_gpio_api = {
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

Driver ch422g_driver = {
    .name = "ch422g",
    .compatible = (const char*[]) { "wch,ch422g", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = static_cast<const void*>(&ch422g_gpio_api),
    .device_type = &GPIO_CONTROLLER_TYPE,
    .owner = &ch422g_module,
    .internal = nullptr
};

}
