#include <Tactility/app/launcher/LauncherMode.h>
#include <Tactility/lvgl/Lvgl.h>

#include <algorithm>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <Tactility/CpuAffinity.h>
#include <generated_assets.h>
#include <Tactility/TactilityConfig.h>
#include <Tactility/lvgl/SystemMenu.h>
#include <Tactility/lvgl/TrackballInit.h>
#include <Tactility/lvgl/UsbHidInput.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/settings/DisplaySettings.h>
#include <Tactility/settings/LauncherSettings.h>
#include <Tactility/settings/Time.h>

#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED
#include <Tactility/settings/TouchCalibrationSettings.h>
#endif

#include <app/event.h>
#include <app/manager.h>

#include <lvgl/devices/keyboard.h>
#include <lvgl/ui_assets.h>
#include <lvgl/fonts.h>
#include <lvgl/devices/pointer.h>
#include <lvgl/widgets/statusbar.h>
#include <lvgl/lvgl.h>
#include <lvgl/module.h>
#include <lvgl/widgets/toolbar.h>

#include <lvgl_window_manager/module.h>
#include <lvgl_window_manager/system_bars.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/drivers/imu.h>
#include <tactility/memory.h>
#include <tactility/module.h>

namespace tt {

namespace service {
    namespace autorotate { extern const ServiceManifest manifest; }
    namespace memorychecker { extern const ServiceManifest manifest; }
    namespace statusbar { extern const ServiceManifest manifest; }
#ifdef ESP_PLATFORM
    namespace displayidle { extern const ServiceManifest manifest; }
    namespace keyboardidle { extern const ServiceManifest manifest; }
#endif
}

namespace lvgl {

static void stopAppFromToolbar(lv_event_t*) {
    // Default nav action for any toolbar that doesn't override it itself. Prefer the topmost
    // new-model app if one is showing; fall back to the old system otherwise (this is what
    // every not-yet-converted app's toolbar still relies on).
    AppInstanceId topmost = 0;
    check(app_manager_get_topmost_instance_id(&topmost) == ERROR_NONE);

    app_event_emit_close(topmost);
}

// The on-screen keyboard widget itself, constructed when the root layout is created
static LvglSoftwareKeyboard softwareKeyboard { .object = nullptr };

static void onScreenCreated(lv_obj_t* root) {
    // Parented to root (not the app container) so it overlays on top of
    // everything, including the statusbar, regardless of which app is showing. Hidden until a
    // focused textarea shows it (see lvgl_keyboard_add_textarea()/textarea_show_keyboard()).
    lvgl_software_keyboard_construct(&softwareKeyboard, root);
}

static SystemBarsLayout resolveSystemBarsLayout(int32_t width, int32_t height) {
    const auto settings = settings::launcher::loadOrGetDefault();
    const auto layout = settings::launcher::resolveSystemBarsLayout(settings, settings::launcher::getSystemBarsCapabilities(), width, height);
    return layout == settings::launcher::SystemBarsLayout::Side ? SYSTEM_BARS_LAYOUT_SIDE : SYSTEM_BARS_LAYOUT_SPLIT;
}

static SystemBarsAlignment resolveSystemBarsAlignment() {
    const auto settings = settings::launcher::loadOrGetDefault();
    return settings.alignment == settings::launcher::SystemBarsAlignment::Right ? SYSTEM_BARS_ALIGNMENT_RIGHT : SYSTEM_BARS_ALIGNMENT_LEFT;
}

#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED

// Applies the calibration persisted by the touch calibration app to the live pointer indev.
// lvgl_devices_attach() runs before onLvglStarted(), so the default indev already exists here.
static void applySavedTouchCalibration() {
    settings::touch::TouchCalibrationSettings settings = settings::touch::loadOrGetDefault();
    if (!settings.enabled || !settings::touch::isValid(settings)) {
        return;
    }

    LvglPointerCalibration calibration = {
        .x_min = settings.xMin,
        .x_max = settings.xMax,
        .y_min = settings.yMin,
        .y_max = settings.yMax,
    };

    lvgl_lock();
    auto* indev = lvgl_pointer_get_default();
    if (indev != nullptr) {
        lvgl_pointer_set_calibration(indev, &calibration);
    }
    lvgl_unlock();
}

#endif // CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED

static void onLvglStarted() {
    // lv_display_create() (inside lvgl_devices_attach(), which already ran by this point) always
    // resets rotation to LV_DISPLAY_ROTATION_0. The only other code that ever applies a saved
    // orientation is the display settings app's dropdown change handler, so without this, every
    // LVGL restart (not just first boot) silently drops back to unrotated. Must run before
    // window_manager_start() below builds the window tree against the display's current size.
    lvgl_lock();
    if (auto* display = lv_display_get_default(); display != nullptr) {
        auto displaySettings = settings::display::loadOrGetDefault();
        lv_display_set_rotation(display, settings::display::toLvglDisplayRotation(displaySettings.orientation));
    }
    // The theme uses LVGL's built-in default font, the generated text font replaces it for every widget that inherits it
    const lv_font_t* text_font = lvgl_get_text_font(FONT_SIZE_DEFAULT);
    lv_obj_set_style_text_font(lv_screen_active(), text_font, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lv_layer_top(), text_font, LV_STATE_DEFAULT);
    lvgl_unlock();

    // The launcher shows the icons right away. Icons that are still current are kept.
    app::launcher::loadPackageIcons(nullptr);
    app::launcher::loadSettingsIcons(nullptr);

    // The statusbar fits itself into the top of round and rounded displays
    lvgl_statusbar_set_menu_callback(systemMenuShow);
    lvgl_statusbar_set_time_format_24h(settings::isTimeFormat24Hour());
    const SystemBarsConfig systemBarsConfig = {
        .show_menu = systemMenuShow,
        .show_notifications = systemMenuShowNotifications,
        .resolve_layout = resolveSystemBarsLayout,
        .resolve_alignment = resolveSystemBarsAlignment,
        .on_screen_created = onScreenCreated,
    };
    system_bars_configure(&systemBarsConfig);
    check(module_ensure_started(&lvgl_window_manager_module) == ERROR_NONE);

    ToolbarConfig toolbar_config = { .nav_action_callback = stopAppFromToolbar };
    lvgl_toolbar_configure(&toolbar_config);

    addService(service::statusbar::manifest);
    addService(service::memorychecker::manifest);
#if defined(ESP_PLATFORM)
    addService(service::displayidle::manifest);
#endif
    if (device_exists_of_type(&IMU_TYPE)) {
        addService(service::autorotate::manifest);
    }
#if defined(CONFIG_TT_TDECK_WORKAROUND)
    addService(service::keyboardidle::manifest);
#endif

    lvgl::startUsbHidInput();
    lvgl::initTrackball();

#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED
    applySavedTouchCalibration();
#endif

    memory_log_stats();
}

static void onLvglStopped() {
    lvgl::stopUsbHidInput();

    if (device_exists_of_type(&IMU_TYPE)) {
        check(service::removeService(service::autorotate::manifest.id));
    }
#if defined(CONFIG_TT_TDECK_WORKAROUND)
    check(service::removeService(service::keyboardidle::manifest.id));
#endif
#if defined(ESP_PLATFORM)
    check(service::removeService(service::displayidle::manifest.id));
#endif
    check(service::removeService(service::memorychecker::manifest.id));
    check(service::removeService(service::statusbar::manifest.id));

    if (softwareKeyboard.object != nullptr) {
        // lv_obj_delete() walks/mutates the object graph (event lists, group membership,
        // parent/child links). Without the LVGL lock this can race the LVGL port task's own
        // concurrent traversal (input dispatch, timers, animations), producing an intermittent
        // double-free/use-after-free inside lv_obj_destructor/lv_event_mark_deleted.
        lvgl_lock();
        lvgl_software_keyboard_destruct(&softwareKeyboard);
        lvgl_unlock();
    }

    module_stop(&lvgl_window_manager_module);

    memory_log_stats();
}

bool isStarted() {
    return module_is_started(&lvgl_module);
}

#define UI_ASSET_IMAGE(name) \
    lv_image_dsc_t { \
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW, .flags = 0, .w = 0, .h = 0, .stride = 0, .reserved_2 = 0 }, \
        .data_size = asset_ui_##name##_png_SIZE, \
        .data = asset_ui_##name##_png, \
        .reserved = nullptr, \
        .reserved_2 = nullptr \
    }

// Ordered by size, see LVGL_UI_SPINNER_COUNT and LVGL_UI_CURSOR_COUNT
static const lv_image_dsc_t spinnerImages[] = {
    UI_ASSET_IMAGE(spinner_12), UI_ASSET_IMAGE(spinner_16), UI_ASSET_IMAGE(spinner_20), UI_ASSET_IMAGE(spinner_24), UI_ASSET_IMAGE(spinner_32), UI_ASSET_IMAGE(spinner_36), UI_ASSET_IMAGE(spinner_40)
};
static const lv_image_dsc_t cursorImages[] = {
    UI_ASSET_IMAGE(cursor_12), UI_ASSET_IMAGE(cursor_16), UI_ASSET_IMAGE(cursor_20), UI_ASSET_IMAGE(cursor_24), UI_ASSET_IMAGE(cursor_32), UI_ASSET_IMAGE(cursor_40)
};

void start() {
    static bool configured = false;
    if (!configured) {
        const LvglUiAssets uiAssets = { spinnerImages, cursorImages };
        lvgl_ui_assets_configure(&uiAssets);
        lvgl_module_configure((LvglModuleConfig) {
            .on_start = onLvglStarted,
            .on_stop = onLvglStopped,
            .task_priority = THREAD_PRIORITY_HIGHER,
            .task_stack_size = 9120,
#ifdef ESP_PLATFORM
            .task_affinity = getCpuAffinityConfiguration().graphics
#endif
        });
        configured = true;
    }
    check(module_ensure_started(&lvgl_module) == ERROR_NONE);
}

void stop() {
    module_stop(&lvgl_module);
}

} // namespace lvgl

} // namespace tt
