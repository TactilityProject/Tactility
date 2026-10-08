#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <cstring>
#include <vector>

#include <lvgl.h>
#include <lvgl/icons/shared.h>
#include <lvgl/theme.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>

#include <Tactility/app/AppGrid.h>
#include <Tactility/app/launcher/LauncherMode.h>
#include <Tactility/app/setup/Setup.h>
#include <Tactility/settings/BootSettings.h>
#include <Tactility/Tactility.h>

namespace tt::app::launcher {

constexpr auto* TAG = "Launcher";

namespace {

std::vector<AppGridItem> collectItems(void* userData);
void onAppClicked(const ::AppManifest& manifest, void* userData);
void onAppLongPressed(const ::AppManifest& manifest, void* userData);
void onAppKey(const ::AppManifest& manifest, uint32_t key, void* userData);

enum class Mode {
    Apps,
    Settings
};

const LauncherMode& getLauncherMode(Mode mode) {
    return mode == Mode::Settings ? SETTINGS_MODE : APPS_MODE;
}

Mode getOtherMode(Mode mode) {
    return mode == Mode::Settings ? Mode::Apps : Mode::Settings;
}

struct Context {
    Mode mode = Mode::Apps;
    lv_obj_t* modeButtonIcon = nullptr;
    AppGrid grid { AppGrid::Callbacks {
        .collect = collectItems,
        .onClicked = onAppClicked,
        .onLongPressed = onAppLongPressed,
        .onKey = onAppKey,
        .userData = this
    } };
};

std::vector<AppGridItem> collectItems(void* userData) {
    return getLauncherMode(static_cast<Context*>(userData)->mode).collect();
}

void startApp(const char* appId) {
    uint32_t instance_id = 0;
    AppStartContext context;
    if (app_start_context_from_id(appId, &context) == ERROR_NONE) {
        app_start_with_context(&context, &instance_id);
    }
}

void onAppClicked(const ::AppManifest& manifest, void*) {
    startApp(manifest.id);
}

void onAppLongPressed(const ::AppManifest& manifest, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    const LauncherMode& mode = getLauncherMode(ctx->mode);
    if (mode.onLongPressed != nullptr) {
        mode.onLongPressed(ctx->grid, manifest);
    }
}

void onAppKey(const ::AppManifest& manifest, uint32_t key, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    const LauncherMode& mode = getLauncherMode(ctx->mode);
    if (mode.onKey != nullptr) {
        mode.onKey(ctx->grid, manifest, key);
    }
}

void applyIconColor(Context* ctx) {
    // The monochrome theme's primary color is black, also on a black background
    if (!lvgl_theme_is_mono()) {
        ctx->grid.setIconColor(getLauncherMode(ctx->mode).iconColor);
    }
}

void onModeButtonPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->mode = getOtherMode(ctx->mode);
    applyIconColor(ctx);
    ctx->grid.showFirstPage();
    ctx->grid.requestRepopulate(false);
    lv_label_set_text(ctx->modeButtonIcon, getLauncherMode(getOtherMode(ctx->mode)).buttonIcon);
}

void onShortcutPressed(lv_event_t* e) {
    startApp(static_cast<const char*>(lv_event_get_user_data(e)));
}

bool isAppRegistered(const char* appId) {
    ::AppManifest manifest;
    return app_manager_find_manifest(appId, &manifest) == ERROR_NONE;
}

bool supportsPowerOff() {
    bool supported = false;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &supported, [](Device* device, void* context) {
        if (device_is_ready(device) && power_supply_supports_power_off(device)) {
            *static_cast<bool*>(context) = true;
            return false; // stop iterating
        } else {
            return true; // continue iterating
        }
    });
    return supported;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    applyIconColor(ctx);
    ctx->grid.setSwipeNavigation(true);
    ctx->grid.createWidgetsWithBottomBar(parent);
    lv_obj_t* modeButton = ctx->grid.addBarButton(getLauncherMode(getOtherMode(ctx->mode)).buttonIcon, onModeButtonPressed, ctx);
    ctx->modeButtonIcon = lv_obj_get_child(modeButton, 0);
    if (isAppRegistered("tactility.poweroff") && supportsPowerOff()) {
        ctx->grid.addBarButton(LVGL_ICON_SHARED_POWER_SETTINGS_NEW, onShortcutPressed, const_cast<char*>("tactility.poweroff"));
    }
}

void runAutoStart() {
    settings::BootSettings boot_properties;
    AppStartContext context;
    if (
        // Auto-start due to built-in requirement
        strcmp(CONFIG_TT_AUTO_START_APP_ID, "") != 0 &&
        app_start_context_from_id(CONFIG_TT_AUTO_START_APP_ID, &context) == ERROR_NONE
    ) {
        LOG_I(TAG, "Starting %s", CONFIG_TT_AUTO_START_APP_ID);
        uint32_t app_launch_id;
        app_start_with_context(&context, &app_launch_id);
    } else if (
        // Auto-start due to user configuration
        settings::loadBootSettings(boot_properties) &&
        !boot_properties.autoStartAppId.empty() &&
        app_start_context_from_id(boot_properties.autoStartAppId.c_str(), &context) == ERROR_NONE
    ) {
        LOG_I(TAG, "Starting %s", boot_properties.autoStartAppId.c_str());
        uint32_t app_launch_id;
        app_start_with_context(&context, &app_launch_id);
    } else {
        // No auto-start, consider running system setup
        if (!setup::isCompleted()) {
            setup::start();
        }
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    Context ctx;
    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    runAutoStart();

    // The launcher is meant to stay resident (it's the home screen) - it only gives up its
    // thread when app-module's scheduler asks it to (e.g. another new-model app is started).
    while (true) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        bool shouldClose = false;
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
        if (shouldClose) break;
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.launcher",
    .name = "Launcher",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 5120, .desired_memory_capability = 0 }
};

} // namespace
