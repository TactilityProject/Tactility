#include <drivers/axp192.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/module.h>

#include <cstring>

extern "C" {

static void configure_axp192(Device* axp192) {
    check(axp192_set_rail_voltage(axp192, AXP192_RAIL_LDO2, 3300) == ERROR_NONE);  // LCD + SD
    check(axp192_set_rail_voltage(axp192, AXP192_RAIL_DCDC3, 3300) == ERROR_NONE); // LCD backlight

    check(axp192_set_rail_enabled(axp192, AXP192_RAIL_LDO2, true) == ERROR_NONE);
    check(axp192_set_rail_enabled(axp192, AXP192_RAIL_LDO3, false) == ERROR_NONE); // VIB_MOTOR stop
    check(axp192_set_rail_enabled(axp192, AXP192_RAIL_DCDC3, true) == ERROR_NONE);

    check(axp192_set_pwm1_duty_cycle(axp192, 255) == ERROR_NONE);   // PWM 255 (LED off)
    check(axp192_set_gpio1_pwm1_output(axp192) == ERROR_NONE);      // GPIO1: PWM
}

static void on_device_event(Device* device, DeviceEvent event, void* context) {
    (void)context;
    if (event == DEVICE_EVENT_STARTED && strcmp(device->name, "axp192") == 0) {
        configure_axp192(device);
    }
}

static error_t start() {
    device_listener_add(on_device_event, nullptr);
    return ERROR_NONE;
}

static error_t stop() {
    device_listener_remove(on_device_event);
    return ERROR_NONE;
}

struct Module m5stack_core2_module = {
    .name = "m5stack-core2",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
