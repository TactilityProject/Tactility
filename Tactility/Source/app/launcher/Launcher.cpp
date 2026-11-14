#include <app/event.h>
#include <app/location.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <atomic>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <lvgl/lvgl.h>
#include <lvgl/theme.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/file_ops.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <Tactility/app/TileGrid.h>
#include <Tactility/app/launcher/LauncherMode.h>
#include <Tactility/app/setup/Setup.h>
#ifdef CONFIG_TT_TOUCH_CALIBRATION_REQUIRED
#include <Tactility/app/touchcalibration/TouchCalibration.h>
#include <Tactility/settings/TouchCalibrationSettings.h>
#endif
#include <Tactility/lvgl/SystemBars.h>
#include <Tactility/settings/BootSettings.h>
#include <Tactility/Tactility.h>

namespace tt::app::launcher {

constexpr auto* TAG = "Launcher";

namespace {

std::vector<TileGridItem> collectItems(void* userData);
void onTilesCleared(void* userData);
void onAppClicked(const TileGridItem& item, void* userData);
void onAppLongPressed(const TileGridItem& item, void* userData);
void onAppKey(const TileGridItem& item, uint32_t key, void* userData);

enum class Mode {
    Apps,
    Settings,
};

const LauncherMode& getLauncherMode(Mode mode) {
    return mode == Mode::Settings ? SETTINGS_MODE : APPS_MODE;
}

Mode getOtherMode(Mode mode) {
    return mode == Mode::Settings ? Mode::Apps : Mode::Settings;
}

struct Context {
    Mode mode = Mode::Apps;
    // Set while the widgets exist, accessed with the LVGL lock held
    bool widgetsExist = false;
    // Set by the package listener, handled by appMain
    TaskEventGroup* eventGroup = nullptr;
    uint32_t packagesChangedBit = 0;
    std::atomic<bool> packagesChanged { false };
    // Set when the settings are shown before their icons are loaded
    std::atomic<bool> settingsIconsRequested { false };
    lv_obj_t* modeButtonIcon = nullptr;
    lvgl::SystemBarsListenerId systemBarsListener = 0;
    TileGrid grid { TileGrid::Callbacks {
        .collect = collectItems,
        .onClicked = onAppClicked,
        .onLongPressed = onAppLongPressed,
        .onKey = onAppKey,
        .resolveImage = nullptr,
        .resolveIcon = nullptr,
        .onPageChanged = nullptr,
        .onTilesCleared = onTilesCleared,
        .userData = this,
    } };
};

std::vector<TileGridItem> collectItems(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    // The theme changed since the icons were rasterized: the file operations task rasterizes them again, then the grid is rebuilt
    const bool appIconsOutdated = !areAppIconsCurrent();
    const bool settingsIconsOutdated = areSettingsIconsLoaded() && !areSettingsIconsCurrent();
    if (appIconsOutdated || settingsIconsOutdated) {
        // Only set, never cleared: a package change from the listener can arrive at the same time
        if (appIconsOutdated) ctx->packagesChanged = true;
        if (settingsIconsOutdated) ctx->settingsIconsRequested = true;
        task_event_group_signal(ctx->eventGroup, ctx->packagesChangedBit);
    }
    return getLauncherMode(ctx->mode).collect();
}

// The tiles that showed replaced icon images are gone
void onTilesCleared(void*) {
    releaseRetiredAppIcons();
    releaseRetiredSettingsIcons();
}

// Runs on the thread that installs or removes the package
void onPackagesChanged(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->packagesChanged = true;
    task_event_group_signal(ctx->eventGroup, ctx->packagesChangedBit);
}

struct StartAppJob {
    std::string appId;
};

// Starting an app can load it from a file, so it runs on the file operations task, which frees the job
void startApp(void* context) {
    auto* job = static_cast<StartAppJob*>(context);
    uint32_t instance_id = 0;
    AppStartContext start_context;
    if (app_start_context_from_id(job->appId.c_str(), &start_context) == ERROR_NONE) {
        app_start_with_context(&start_context, &instance_id);
    }
    delete job;
}

void onAppClicked(const TileGridItem& item, void*) {
    auto* job = new (::std::nothrow) StartAppJob { item.id };
    if (job == nullptr || file_ops_queue(startApp, job, nullptr) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue starting %s", item.id.c_str());
        delete job;
    }
}

void onAppLongPressed(const TileGridItem& item, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    const LauncherMode& mode = getLauncherMode(ctx->mode);
    if (mode.onLongPressed != nullptr) {
        mode.onLongPressed(ctx->grid, item);
    }
}

void onAppKey(const TileGridItem& item, uint32_t key, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    const LauncherMode& mode = getLauncherMode(ctx->mode);
    if (mode.onKey != nullptr) {
        mode.onKey(ctx->grid, item, key);
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
    if (ctx->mode == Mode::Settings && !areSettingsIconsCurrent()) {
        ctx->settingsIconsRequested = true;
        task_event_group_signal(ctx->eventGroup, ctx->packagesChangedBit);
    }
    applyIconColor(ctx);
    ctx->grid.showFirstPage();
    ctx->grid.requestRepopulate(false);
    lv_label_set_text(ctx->modeButtonIcon, getLauncherMode(getOtherMode(ctx->mode)).buttonIcon);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    applyIconColor(ctx);
    ctx->grid.setSwipeNavigation(true);
    // In the side layout, the bar's buttons are in the system bars strip
    ctx->grid.createWidgetsWithBottomBar(parent, lvgl::systemBarsGetActionContainer());
    lv_obj_t* modeButton = ctx->grid.addBarButton(getLauncherMode(getOtherMode(ctx->mode)).buttonIcon, onModeButtonPressed, ctx);
    ctx->modeButtonIcon = lv_obj_get_child(modeButton, 0);
    // After all bar buttons, as they change the size of the grid
    ctx->grid.finishWidgetsWithBottomBar();

    ctx->systemBarsListener = lvgl::systemBarsAddListener([ctx] {
        ctx->grid.setBarSideContainer(lvgl::systemBarsGetActionContainer());
    });
    ctx->widgetsExist = true;
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->widgetsExist = false;
    lvgl::systemBarsRemoveListener(ctx->systemBarsListener);
    ctx->systemBarsListener = 0;
    ctx->grid.destroyWidgets();
}

#ifdef CONFIG_TT_TOUCH_CALIBRATION_REQUIRED
// Reads the settings file, so it runs on the file operations task
bool isTouchCalibrated() {
    settings::touch::TouchCalibrationSettings calibration;
    return settings::touch::load(calibration) && calibration.enabled && settings::touch::isValid(calibration);
}
#endif

struct AutoStartJob {
    uint32_t launcherInstanceId;
    // The touch calibration finished since the launcher started
    bool touchCalibrated;
};

// Reads settings files, so it runs on the file operations task, which frees the job.
// The job is not in the context of the app: queued work still runs after the app has closed.
void runAutoStart(void* context) {
    const auto job = *static_cast<AutoStartJob*>(context);
    delete static_cast<AutoStartJob*>(context);

    enum class Target { None, App, Setup };
    Target target = Target::None;
    settings::BootSettings boot_properties;
    AppStartContext context_to_start;
    if (
        // Auto-start due to built-in requirement
        strcmp(CONFIG_TT_AUTO_START_APP_ID, "") != 0 &&
        app_start_context_from_id(CONFIG_TT_AUTO_START_APP_ID, &context_to_start) == ERROR_NONE
    ) {
        LOG_I(TAG, "Starting %s", CONFIG_TT_AUTO_START_APP_ID);
        target = Target::App;
    } else if (
        // Auto-start due to user configuration
        settings::loadBootSettings(boot_properties) &&
        !boot_properties.autoStartAppId.empty() &&
        app_start_context_from_id(boot_properties.autoStartAppId.c_str(), &context_to_start) == ERROR_NONE
    ) {
        LOG_I(TAG, "Starting %s", boot_properties.autoStartAppId.c_str());
        target = Target::App;
    } else if (!setup::isCompleted()) {
        // No auto-start, consider running system setup
        target = Target::Setup;
    }

#ifdef CONFIG_TT_TOUCH_CALIBRATION_REQUIRED
    // The device isn't usable without a calibration, so it runs first. The setup always starts with it, also when
    // one was saved, so that a calibration that doesn't work can be redone by restarting before the setup is finished.
    // The launcher runs this again when the calibration ended.
    if (!job.touchCalibrated && (target == Target::Setup || !isTouchCalibrated())) {
        LOG_I(TAG, "Starting touch calibration");
        touchcalibration::start(job.launcherInstanceId);
        return;
    }
#endif

    if (target == Target::App) {
        uint32_t app_launch_id;
        app_start_with_context(&context_to_start, &app_launch_id);
    } else if (target == Target::Setup) {
        setup::start();
    }
}

// Queues the auto start, which needs the launcher's instance id to receive the result of a calibration
void queueAutoStart(uint32_t launcherInstanceId, bool touchCalibrated) {
    auto* job = new (::std::nothrow) AutoStartJob { launcherInstanceId, touchCalibrated };
    if (job == nullptr || file_ops_queue(runAutoStart, job, nullptr) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue the auto start");
        delete job;
    }
}

// What a queued icon job loads
struct IconJob {
    bool packages;
    bool settings;
};

// Rasterizes icons, so it runs on the file operations task, which frees the job.
// The job is not in the context of the app: queued work still runs after the app has closed.
void loadIcons(void* context) {
    auto* job = static_cast<IconJob*>(context);
    if (job->packages) loadPackageIcons(nullptr);
    if (job->settings) loadSettingsIcons(nullptr);
    delete job;
}

// Reads files, so it runs on the file operations task
void loadLauncherData(void*) {
    loadFavourites(nullptr);
    loadPackageIcons(nullptr);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    FileOpsSubscription file_ops_sub {};
    check(file_ops_subscribe(&file_ops_sub, &event_group) == ERROR_NONE);

    Context ctx;
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.packagesChangedBit) == ERROR_NONE);
    check(app_manager_add_package_listener(onPackagesChanged, &ctx) == ERROR_NONE);

    WindowId window = 0;
    bool windowCreated = false;
    bool iconsReloading = false;
#ifdef CONFIG_TT_TOUCH_CALIBRATION_REQUIRED
    bool touchCalibrationFinished = false;
#endif

    // The window is created once the favourites are loaded, so its first render has the final order
    check(file_ops_queue(loadLauncherData, nullptr, &file_ops_sub) == ERROR_NONE);

    // The launcher is meant to stay resident (it's the home screen) - it only gives up its
    // thread when app-module's scheduler asks it to (e.g. another new-model app is started).
    while (true) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        if (!windowCreated && file_ops_poll(&file_ops_sub) == ERROR_NONE) {
            window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);
            windowCreated = true;
            // The file operations run in order: the favourites are available when auto-start runs
            queueAutoStart(appInstanceId, false);
        }

        if (windowCreated && iconsReloading && file_ops_poll(&file_ops_sub) == ERROR_NONE) {
            iconsReloading = false;
            // A buried window has no widgets: it collects the items again when it surfaces
            // Replaced images are freed by the rebuild, or right away when there are no tiles
            lvgl_lock();
            if (ctx.widgetsExist) {
                ctx.grid.requestRepopulate(true);
            } else {
                releaseRetiredAppIcons();
                releaseRetiredSettingsIcons();
            }
            lvgl_unlock();
        }
        if (windowCreated && !iconsReloading) {
            const bool packages = ctx.packagesChanged.exchange(false);
            const bool settings = ctx.settingsIconsRequested.exchange(false);
            if (packages || settings) {
                auto* job = new (::std::nothrow) IconJob { packages, settings };
                if (job != nullptr && file_ops_queue(loadIcons, job, &file_ops_sub) == ERROR_NONE) {
                    iconsReloading = true;
                } else {
                    LOG_E(TAG, "Failed to queue loading the icons");
                    delete job;
                    // Tried again when the next request arrives
                    if (packages) ctx.packagesChanged = true;
                    if (settings) ctx.settingsIconsRequested = true;
                }
            }
        }

        bool shouldClose = false;
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
#ifdef CONFIG_TT_TOUCH_CALIBRATION_REQUIRED
            // The launcher only starts a child for the touch calibration. The auto start runs again to check the result
            if (event.type == APP_EVENT_RESULT) {
                app_manager_stop(event.result.launch_id);
                // A cancelled or failed calibration starts again. The result is 0 when it was applied
                touchCalibrationFinished = touchCalibrationFinished || event.result.result == 0;
                queueAutoStart(appInstanceId, touchCalibrationFinished);
            }
#endif
        }
        if (shouldClose) break;
    }

    if (windowCreated) {
        window_manager_remove(window);
    }
    // No tile shows an icon image anymore
    releaseRetiredAppIcons();
    releaseRetiredSettingsIcons();
    app_manager_remove_package_listener(onPackagesChanged, &ctx);
    task_event_group_release_bit(&event_group, ctx.packagesChangedBit);
    file_ops_unsubscribe(&file_ops_sub);
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
    // All file operations run on the file operations task, so the stack can be in external RAM
    .stack = { .depth = 3072, .desired_memory_capability = MEMORY_CAPABILITY_EXTERNAL },
};

} // namespace
