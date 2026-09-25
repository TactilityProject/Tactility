#include "tactility/memory.h"


#include <Tactility/app/terminal/Terminal.h>

#include <tactility/log.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/module.h>
#include <lvgl/module.h>

constexpr auto* TAG = "terminal";

namespace tt::app::terminal {

int main(int argc, char* argv[]) {
    Device* display_device = nullptr;
    if (device_get_first_active_by_type(&DISPLAY_TYPE, &display_device) != ERROR_NONE) {
        LOG_E(TAG, "No display device found");
        return 0;
    }

    // Stop LVGL, because terminal has custom rendering
    auto lvgl_active = module_is_started(&lvgl_module);
    if (lvgl_active) {
        module_stop(&lvgl_module);
    }

    runTerminal(display_device);

    device_put(display_device);

    // If needed, restart LVGL
    if (lvgl_active && !module_is_started(&lvgl_module)) {
        LOG_I(TAG, "Restarting LVGL");
        module_start(&lvgl_module);
    }

    return 0;
}

extern const ::AppManifest manifest = {
    .id = "tactility.terminal",
    .name = "Terminal",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = 0,
    .stack = { .depth = 5120, .desired_memory_capability = 0 },
};

}
