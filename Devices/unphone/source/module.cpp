#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/driver.h>
#include <tactility/error.h>
#include <tactility/module.h>

#include <cstring>

bool init_boot();

extern "C" {

extern Driver unphone_power_switch_driver;
extern Driver unphone_nav_buttons_driver;

// The root device (the only device with no parent) reaching DEVICE_EVENT_STARTED means the
// whole devicetree has finished constructing/starting, so it's now safe to power on the
// unPhone-specific peripherals that initBoot() depends on (e.g. the bq24295 fuel gauge).
static void on_device_event(Device* device, DeviceEvent event, void* context) {
    (void)context;

    static bool has_power_switch = false;
    static bool has_bq24295 = false;
    static bool has_backlight = false;
    static bool did_init = false;

    if (event == DEVICE_EVENT_STARTED) {
        if (strcmp(device->name, "power_switch") == 0) { has_power_switch = true; }
        if (strcmp(device->name, "bq24295") == 0) { has_bq24295 = true; }
        if (strcmp(device->name, "display_backlight") == 0) { has_backlight = true; }
    }

    if (!did_init && has_power_switch && has_bq24295 && has_backlight) {
        check(init_boot(), "unPhone initBoot failed");
        did_init = true;
    }
}

static error_t start() {
    device_listener_add(on_device_event, nullptr);
    return ERROR_NONE;
}

static error_t stop() {
    device_listener_remove(&on_device_event);
    return ERROR_NONE;
}

static const Driver* unphone_drivers[] = {
    &unphone_nav_buttons_driver,
    &unphone_power_switch_driver,
    nullptr
};

Module unphone_module = {
    .name = "unphone",
    .start = start,
    .stop = stop,
    .drivers = unphone_drivers
};

}
