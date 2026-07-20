#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct Tab5KeyboardConfig {
    // Fixed 0x6D on this keyboard accessory - kept as a field for parity/documentation with
    // other dynamically-constructed devices in this project, not used to probe the bus.
    uint8_t address;
    // Native SoC GPIO (e.g. gpio0 pin 50) wired directly to the keyboard's INT line - not routed
    // through an IO expander, since it needs a real hardware ISR for responsive key events.
    // GPIO_PIN_SPEC_NONE falls back to polling REG_INT_STAT instead.
    struct GpioPinSpec pin_interrupt;
};

// Called when the keyboard accessory's hot-plug attach state changes (confirmed over two
// consecutive ~1s checks - see the driver source). Orientation changes and other LVGL-aware
// reactions to attach state live outside the driver (it has no LVGL dependency) - module.cpp
// registers a listener for this instead.
// @return true once handled; false to be called again on the next confirmed check with the same
// `attached` value (e.g. if the LVGL lock couldn't be acquired) - mirrors this driver's own
// internal retry-until-handled pattern for reinitializing the device on reattach.
typedef bool (*Tab5KeyboardAttachListener)(struct Device* device, bool attached, void* context);

// Only one listener is supported (this board only ever has one caller - module.cpp). Registering
// a new one before removing the previous replaces it with a warning logged.
void tab5_keyboard_add_attach_listener(struct Device* device, Tab5KeyboardAttachListener callback, void* context);
void tab5_keyboard_remove_attach_listener(struct Device* device, Tab5KeyboardAttachListener callback);

extern struct Driver tab5_keyboard_driver;

// Constructs and starts the keyboard accessory device on i2c2, then registers this project's own
// hot-plug rotation handler as its attach listener. Called from display_detect.cpp's
// on_display_detect_event() once i2c2 is up.
void tab5_create_keyboard(struct Device* i2c2);

#ifdef __cplusplus
}
#endif
