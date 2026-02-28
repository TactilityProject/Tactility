#pragma once

#include "gpio.h"

struct Device;

struct GpioDescriptor {
    /** @brief The controller that owns this pin */
    struct Device* controller;
    /** @brief Physical pin number */
    gpio_pin_t pin;
    /** @brief Current owner */
    enum GpioOwnerType owner_type;
    /**
     * @brief Implementation-specific context (e.g. from esp32 controller internally)
     * Unlike other drivers, a GPIO controller's internal data is created and set by gpio_controller_init_descriptors()
     * This means that the specific controller implementation cannot set the device's driver data, as it's already set by the GPIO controller base coded.
     * When calling init descriptors, the caller can pass a controller_context, which is an optional pointer that holds the implementation's internal data.
     */
    void* controller_context;
};
