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
    /** @brief Implementation-specific context (e.g. from esp32 controller internally) */
    void* controller_context;
};
