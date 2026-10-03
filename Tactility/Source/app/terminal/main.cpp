#include "tactility/memory.h"


#include <Tactility/app/terminal/Terminal.h>
#include <Tactility/app/terminal/TerminalRendererGeneric.h>
#include <Tactility/app/terminal/TerminalRendererLvgl.h>
#include <Tactility/app/terminal/TerminalRendererPpa.h>

#include <tactility/log.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/module.h>
#include <lvgl/devices/indev.h>
#include <lvgl/module.h>
#include <lvgl/widgets/toolbar.h>
#include <lvgl_window_manager/window_manager.h>

#include <cstring>

constexpr auto* TAG = "terminal";

namespace tt::app::terminal {

namespace {

struct WindowContext {
    TerminalRendererLvgl renderer;
    AppInstanceId appInstanceId;
};

// The toolbar's default action closes the topmost app instance, which is the shell app rather than this one.
void onClosePressed(lv_event_t* event) {
    auto* ctx = static_cast<WindowContext*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* root, void* userData) {
    auto* ctx = static_cast<WindowContext*>(userData);

    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(root, 0, LV_STATE_DEFAULT);

    // The toolbar only offers a touch close button, so it is left out to give the terminal more room
    if (lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
        auto* toolbar = lvgl_toolbar_create(root, "Terminal");
        lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onClosePressed, ctx);
    }

    auto* canvas = lv_canvas_create(root);
    lv_obj_set_width(canvas, LV_PCT(100));
    lv_obj_set_flex_grow(canvas, 1);

    ctx->renderer.attachCanvas(canvas);
}

void destroyWidgets(void* userData) {
    static_cast<WindowContext*>(userData)->renderer.attachCanvas(nullptr);
}

bool hasArgument(int argc, char* argv[], const char* argument) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], argument) == 0) {
            return true;
        }
    }
    return false;
}

void runInWindow(Device* display) {
    WindowContext ctx { .appInstanceId = app_scheduler_current_app_id() };
    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);
    runTerminal(display, ctx.renderer, false);
    window_manager_remove(window);
}

void runFullscreen(Device* display) {
    // Stop LVGL, because terminal has custom rendering
    auto lvgl_active = module_is_started(&lvgl_module);
    if (lvgl_active) {
        module_stop(&lvgl_module);
    }

    TerminalRendererPpa ppaRenderer;
    TerminalRendererGeneric genericRenderer;
    TerminalRenderer& renderer = TerminalRendererPpa::isSupported()
        ? static_cast<TerminalRenderer&>(ppaRenderer)
        : static_cast<TerminalRenderer&>(genericRenderer);
    runTerminal(display, renderer, true);

    // If needed, restart LVGL
    if (lvgl_active && !module_is_started(&lvgl_module)) {
        LOG_I(TAG, "Restarting LVGL");
        module_start(&lvgl_module);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    Device* display_device = nullptr;
    if (device_get_first_active_by_type(&DISPLAY_TYPE, &display_device) != ERROR_NONE) {
        LOG_E(TAG, "No display device found");
        return 0;
    }

    // The canvas holds a full frame, which needs external RAM on larger displays.
    const bool useLvgl = module_is_started(&lvgl_module) &&
        memory_external_total() > 0 &&
        !hasArgument(argc, argv, "--no-lvgl");

    if (useLvgl) {
        runInWindow(display_device);
    } else {
        runFullscreen(display_device);
    }

    device_put(display_device);

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
