#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

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

uint32_t settingsInstanceId = 0;

void onAppPressed(lv_event_t* e) {
    // Fire-and-forget top-level navigation, same as AppList's own app-launch buttons.
    const auto* manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    uint32_t instanceId = 0;
    app_manager_start(manifest->id, &instanceId);
}

void onBackPressed(lv_event_t*) {
    // The global toolbar nav callback only knows how to stop old-model apps, so this
    // new-model app overrides its own toolbar's nav action to close itself instead. Async,
    // non-blocking - see AppList.cpp's onBackPressed() for why this must not call
    // app_manager_stop() directly (would deadlock against the LVGL lock).
    AppEvent event { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(settingsInstanceId, &event);
}

void createWidget(const ::AppManifest* manifest, lv_obj_t* list) {
    check(list);
    // The new AppManifest has no per-app icon - use a shared generic one for every entry,
    // same fallback the old model used for apps that didn't provide one.
    auto* btn = lv_list_add_button(list, LVGL_ICON_SHARED_TOOLBAR, manifest->name);
    lv_obj_t* image = lv_obj_get_child(btn, 0);
    lv_obj_set_style_text_font(image, lvgl_get_shared_icon_font(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, &onAppPressed, LV_EVENT_SHORT_CLICKED, const_cast<::AppManifest*>(manifest));
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<const ::AppManifest*>*>(context);
    manifests->push_back(manifest);
}

void createWidgets(lv_obj_t* parent, void*) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Settings");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, nullptr);

    auto* list = lv_list_create(parent);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);

    std::vector<const ::AppManifest*> manifests;
    app_manager_for_each_manifest(collectManifest, &manifests);
    std::ranges::sort(manifests, [](const ::AppManifest* a, const ::AppManifest* b) {
        return strcmp(a->name, b->name) < 0;
    });

    for (const auto* manifest: manifests) {
        if (manifest->category == APP_CATEGORY_SETTINGS && (manifest->flags & APP_MANIFEST_FLAG_HIDDEN) == 0) {
            createWidget(manifest, list);
        }
    }
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    settingsInstanceId = appInstanceId;

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, nullptr);

    while (true) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        if (event.type == APP_EVENT_CLOSE) {
            app_manager_finish(appInstanceId);
            break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "Settings",
    .name = "Settings",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
