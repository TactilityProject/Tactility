#pragma once

#include "gpio.h"

struct GpioDescriptor {
    /** @brief The controller that owns this pin */
    struct Device* controller;
    /** @brief Physical pin number */
    gpio_pin_t pin;
    /** @brief Current owner */
    enum GpioOwnerType owner_type;
    /** @brief Owner identity for validation */
    void* owner_context;
    /** @brief Pin level */
    gpio_level_t level;
    /** @brief Pin configuration flags */
    gpio_flags_t flags;
    /** @brief Implementation-specific context (e.g. from esp32 controller internally) */
    void* controller_context;
};
