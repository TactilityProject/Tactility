#include <lvgl/icons/shared.h>
#include <lvgl/fonts.h>

#include <Tactility/app/appdetails/AppDetails.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/widgets/toolbar.h>
#include <lvgl.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace tt::app::appsettings {

extern const ::AppManifest manifest;

namespace {

// Set by appMain() right before window_manager_create(), read by onBackPressed().
uint32_t appSettingsInstanceId = 0;

void onAppPressed(lv_event_t* e) {
    const auto* target_manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    appdetails::start(target_manifest->id);
}

void onBackPressed(lv_event_t*) {
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent event { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(appSettingsInstanceId, &event);
}

void createAppWidget(const ::AppManifest* target_manifest, lv_obj_t* list) {
    // The new AppManifest has no per-app icon - use a shared generic one for every entry, same
    // fallback AppList.cpp uses.
    lv_obj_t* btn = lv_list_add_button(list, LVGL_ICON_SHARED_TOOLBAR, target_manifest->name);
    lv_obj_t* image = lv_obj_get_child(btn, 0);
    lv_obj_set_style_text_font(image, lvgl_get_shared_icon_font(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, &onAppPressed, LV_EVENT_SHORT_CLICKED, const_cast<::AppManifest*>(target_manifest));
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<const ::AppManifest*>*>(context);
    manifests->push_back(manifest);
}

void createWidgets(lv_obj_t* parent, void*) {
    auto* toolbar = lvgl_toolbar_create(parent, "Installed Apps");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, nullptr);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* list = lv_list_create(parent);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_align_to(list, toolbar, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    auto toolbar_height = lv_obj_get_height(toolbar);
    auto parent_content_height = lv_obj_get_content_height(parent);
    lv_obj_set_height(list, parent_content_height - toolbar_height);

    std::vector<const ::AppManifest*> manifests;
    app_manager_for_each_manifest(collectManifest, &manifests);
    std::ranges::sort(manifests, [](const ::AppManifest* a, const ::AppManifest* b) {
        return strcmp(a->name, b->name) < 0;
    });

    size_t app_count = 0;
    for (const auto* target_manifest: manifests) {
        if (target_manifest->location.type == APP_LOCATION_PATH) {
            app_count++;
            createAppWidget(target_manifest, list);
        }
    }

    if (app_count == 0) {
        auto* no_apps_label = lv_label_create(parent);
        lv_label_set_text(no_apps_label, "No apps installed");
        lv_obj_align(no_apps_label, LV_ALIGN_CENTER, 0, 0);
    }
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    appSettingsInstanceId = appInstanceId;

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, nullptr);

    bool shouldClose = false;
    while (!shouldClose) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        switch (event.type) {
            case APP_EVENT_CLOSE:
                app_manager_finish(appInstanceId);
                shouldClose = true;
                break;
            default:
                break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "AppSettings",
    .name = "Apps",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace
