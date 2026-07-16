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
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&unphone_power_switch_driver) == ERROR_NONE);
    check(driver_construct_add(&unphone_nav_buttons_driver) == ERROR_NONE);
    device_listener_add(on_device_event, nullptr);
    return ERROR_NONE;
}

static error_t stop() {
    device_listener_remove(&on_device_event);
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&unphone_nav_buttons_driver) == ERROR_NONE);
    check(driver_remove_destruct(&unphone_power_switch_driver) == ERROR_NONE);
    return ERROR_NONE;
}

struct Module unphone_module = {
    .name = "unphone",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
