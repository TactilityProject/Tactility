#include "detect.h"

#include "../../../../TactilityKernel/include/tactility/log.h"
#include "devices_common.h"
#include "devices_v1.h"
#include "devices_v2.h"
#include "tab5_keyboard.h"

#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

#include <cstring>

constexpr auto* TAG = "Tab5";

static Device tab5_power_control_device {};

// Charge-control/quick-charge/power-off for the IP2326 charger IC, driven through io_expander1 (see tab5_power_control.h).
static void tab5_create_power_control(Device* io_expander1) {
    tab5_power_control_device = Device {
        .address = 0,
        .name = "power_control0",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr,
    };

    // Parented to io_expander1 itself: the driver's start()/set_*() use device_get_parent() as
    // its GPIO controller.
    construct_add_start(&tab5_power_control_device, io_expander1, "m5stack,tab5-power-control");
}

constexpr auto GPIO_EXP0_PIN_LCD_RESET = 4;
constexpr auto GPIO_EXP0_PIN_TOUCH_RESET = 5;

bool pulse_display_reset_pins(Device* io_expander0) {
    auto* lcd_reset_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_LCD_RESET, GPIO_OWNER_GPIO);
    auto* touch_reset_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_TOUCH_RESET, GPIO_OWNER_GPIO);
    if (lcd_reset_pin == nullptr || touch_reset_pin == nullptr) {
        LOG_E(TAG, "display_detect: failed to acquire LCD/touch reset pins on io_expander0");
        if (lcd_reset_pin != nullptr) {
            gpio_descriptor_release(lcd_reset_pin);
        }
        if (touch_reset_pin != nullptr) {
            gpio_descriptor_release(touch_reset_pin);
        }
        return false;
    }

    gpio_descriptor_set_flags(lcd_reset_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(touch_reset_pin, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_level(lcd_reset_pin, false);
    gpio_descriptor_set_level(touch_reset_pin, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_descriptor_set_level(lcd_reset_pin, true);
    gpio_descriptor_set_level(touch_reset_pin, true);

    gpio_descriptor_release(lcd_reset_pin);
    gpio_descriptor_release(touch_reset_pin);
    return true;
}

// Fires for every device's start/stop in the system.
static void on_display_detect_event(Device* device, DeviceEvent event, void* context) {
    (void)context;

    static Device* i2c0 = nullptr;
    static Device* io_expander0 = nullptr;
    static bool did_create_display = false;

    static Device* i2c2 = nullptr;
    static bool did_create_keyboard = false;

    static Device* io_expander1 = nullptr;
    static bool did_create_power_control = false;

    if (event == DEVICE_EVENT_STARTED) {
        if (strcmp(device->name, "i2c0") == 0) {
            i2c0 = device;
        }
        if (strcmp(device->name, "io_expander0") == 0) {
            io_expander0 = device;
        }
        if (strcmp(device->name, "i2c2") == 0) {
            i2c2 = device;
        }
        if (strcmp(device->name, "io_expander1") == 0) {
            io_expander1 = device;
        }
    }

    if (io_expander0 != nullptr && tab5_get_variant() == Tab5Variant::Unknown) {
        Tab5Variant variant = tab5_probe_variant(i2c0);
        tab5_set_variant(variant);
    }

    // We need i2c0 and io_expander0 to create the display and touch devices
    if (!did_create_display && i2c0 != nullptr && io_expander0 != nullptr) {
        did_create_display = true;
        if (pulse_display_reset_pins(io_expander0)) {
            switch (tab5_get_variant()) {
                case Tab5Variant::Unknown:
                    LOG_E(TAG, "Variant not detected yet");
                    break;
                case Tab5Variant::V1:
                    tab5_create_devices_v1(i2c0);
                    break;
                case Tab5Variant::V2:
                    tab5_create_devices_v2(i2c0);
                    break;
            }
        } else {
            LOG_E(TAG, "display_detect: skipping display creation, failed to pulse reset pins");
        }
    }

    // The keyboard lives on the unrelated i2c2 bus, so this is gated independently.
    if (!did_create_keyboard && i2c2 != nullptr) {
        did_create_keyboard = true;
        tab5_create_keyboard(i2c2);
    }

    // The charger control lines live on io_expander1, unrelated to the display/keyboard buses.
    if (!did_create_power_control && io_expander1 != nullptr) {
        did_create_power_control = true;
        tab5_create_power_control(io_expander1);
    }
}

void tab5_detect_start() {
    device_listener_add(on_display_detect_event, nullptr);
}

void tab5_detect_stop() {
    device_listener_remove(on_display_detect_event);
}
