#include <drivers/axp2101.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/module.h>

#include <cstring>

extern "C" {

// M5Stack CoreS3 AXP2101 rail setup, ported from the board's old deprecated-HAL
// initPowerControl() (source: M5Unified's Power_Class.cpp). ALDO1 powers the AW88298 audio
// amp, ALDO2 the ES7210 microphone ADC, ALDO3 the GC0308 camera, ALDO4 the TF/SD card slot.
// DLDO1 (LCD backlight) is left to the axp2101-backlight child device.
static void configure_axp2101(Device* axp2101) {
    check(axp2101_set_ldo_voltage(axp2101, AXP2101_ALDO1, 1800) == ERROR_NONE);
    check(axp2101_set_ldo_voltage(axp2101, AXP2101_ALDO2, 3300) == ERROR_NONE);
    check(axp2101_set_ldo_voltage(axp2101, AXP2101_ALDO3, 3300) == ERROR_NONE);
    check(axp2101_set_ldo_voltage(axp2101, AXP2101_ALDO4, 3300) == ERROR_NONE);

    check(axp2101_set_ldo_enabled(axp2101, AXP2101_ALDO1, true) == ERROR_NONE);
    check(axp2101_set_ldo_enabled(axp2101, AXP2101_ALDO2, true) == ERROR_NONE);
    check(axp2101_set_ldo_enabled(axp2101, AXP2101_ALDO3, true) == ERROR_NONE);
    check(axp2101_set_ldo_enabled(axp2101, AXP2101_ALDO4, true) == ERROR_NONE);
    check(axp2101_set_ldo_enabled(axp2101, AXP2101_BLDO1, true) == ERROR_NONE);
    check(axp2101_set_ldo_enabled(axp2101, AXP2101_BLDO2, true) == ERROR_NONE);
}

static void on_device_event(Device* device, DeviceEvent event, void* context) {
    (void)context;
    if (event == DEVICE_EVENT_STARTED && strcmp(device->name, "axp2101") == 0) {
        configure_axp2101(device);
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

struct Module m5stack_cores3_module = {
    .name = "m5stack-cores3",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
