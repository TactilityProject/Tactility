#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/icons/shared.h>
#include <lvgl/fonts.h>
#include <lvgl/widgets/toolbar.h>
#include <tactility/check.h>

#include <lvgl.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace tt::app::settings {

namespace {

struct Context {
    uint32_t appInstanceId;
    // Owned copies backing every button's user data, so a button never references the ledger
    // directly - safe even if an external app is uninstalled while Settings stays open. Built
    // once, in createWidgets() below (this list is never repopulated).
    std::vector<::AppManifest> manifests;
};

struct IconEntry { 
    const char* id;
    const char* icon;
};

constexpr IconEntry ICONS[] = {
    {"tactility.apppackagelist",    LVGL_ICON_SHARED_DEPLOYED_CODE},
    {"tactility.audiosettings",     LVGL_ICON_SHARED_MUSIC_NOTE},
    {"tactility.btmanage",          LVGL_ICON_SHARED_BLUETOOTH},
    {"tactility.development",       LVGL_ICON_SHARED_LOGO_DEV},
    {"tactility.display",           LVGL_ICON_SHARED_DISPLAY_SETTINGS},
    {"tactility.gpssettings",       LVGL_ICON_SHARED_NAVIGATION},
    {"tactility.grovesettings",     LVGL_ICON_SHARED_CABLE},
    {"tactility.keyboardsettings",  LVGL_ICON_SHARED_KEYBOARD_ALT},
    {"tactility.localesettings",    LVGL_ICON_SHARED_LANGUAGE},
    {"tactility.power",             LVGL_ICON_SHARED_POWER_SETTINGS_NEW},
    {"tactility.timedatesettings",  LVGL_ICON_SHARED_CALENDAR_MONTH},
    {"tactility.touchcalibration",  LVGL_ICON_SHARED_CIRCLE},
    {"tactility.trackballsettings", LVGL_ICON_SHARED_DEVICES},
    {"tactility.usbsettings",       LVGL_ICON_SHARED_USB},
    {"tactility.wifimanage",        LVGL_ICON_SHARED_WIFI},
};

const char* appIcon(const ::AppManifest* manifest) {
    for (const auto& entry : ICONS) {
        if (!strcmp(manifest->id, entry.id)) return entry.icon;
    }
    return LVGL_ICON_SHARED_TOOLBAR;
}

void onAppPressed(lv_event_t* e) {
    // Fire-and-forget top-level navigation, same as AppList's own app-launch buttons.
    const auto* manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    uint32_t instanceId = 0;
    // Re-resolved by id against the live ledger, not the (possibly stale) cached manifest above:
    // fails gracefully if the app was uninstalled since this button was built, instead of handing
    // app_manager_start_internal() a pointer it would store for the new instance's whole lifetime.
    AppStartContext context;
    if (app_start_context_from_id(manifest->id, &context) == ERROR_NONE) {
        app_start_with_context(&context, &instanceId);
    }
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidget(const ::AppManifest* manifest, lv_obj_t* list) {
    check(list);
    auto* btn = lv_list_add_button(list, appIcon(manifest), manifest->name);
    lv_obj_t* image = lv_obj_get_child(btn, 0);
    lv_obj_set_style_text_font(image, lvgl_get_shared_icon_font(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, &onAppPressed, LV_EVENT_SHORT_CLICKED, const_cast<::AppManifest*>(manifest));
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<::AppManifest>*>(context);
    manifests->push_back(*manifest);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Settings");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* list = lv_list_create(parent);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);

    std::vector<::AppManifest> collected;
    app_manager_for_each_manifest(collectManifest, &collected);
    std::ranges::sort(collected, [](const ::AppManifest& a, const ::AppManifest& b) {
        return strcmp(a.name, b.name) < 0;
    });
    ctx->manifests = std::move(collected);

    for (const auto& manifest: ctx->manifests) {
        if (manifest.category == APP_CATEGORY_SETTINGS && (manifest.flags & APP_MANIFEST_FLAG_HIDDEN) == 0) {
            createWidget(&manifest, list);
        }
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx { appInstanceId };

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

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
    .id = "tactility.settings",
    .name = "Settings",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 2400, .desired_memory_capability = 0 },
};

} // namespace
