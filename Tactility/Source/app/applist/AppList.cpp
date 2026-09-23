#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/applist/Favourites.h>

#include <tactility/check.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include <lvgl/icons/shared.h>
#include <lvgl/fonts.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::applist {

namespace {

struct Context {
    uint32_t appInstanceId;
    Favourites favourites;
};

void populateList(lv_obj_t* list);

void onAppPressed(lv_event_t* e) {
    const auto* manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    uint32_t instanceId = 0;
    if (app_start(manifest->id, 0, nullptr, &instanceId) == ERROR_NONE) {
        lv_obj_t* list = lv_obj_get_parent(lv_event_get_target_obj(e));
        auto* ctx = static_cast<Context*>(lv_obj_get_user_data(list));
        app_event_emit_close(ctx->appInstanceId);
    }
}

void onAppLongPressed(lv_event_t* e) {
    const auto* manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    lv_obj_t* list = lv_obj_get_parent(lv_event_get_target_obj(e));
    auto* ctx = static_cast<Context*>(lv_obj_get_user_data(list));
    ctx->favourites.toggle(manifest->id);
    // Deferred: populateList() deletes this button via lv_obj_clean() while its own long-press is still dispatching.
    lv_async_call([](void* userData) {
        populateList(static_cast<lv_obj_t*>(userData));
    }, list);
}

void onAppKeyPressed(lv_event_t* e) {
    const uint32_t key = lv_event_get_key(e);
    if (key != 'f' && key != 'F') {
        return;
    }
    const auto* manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    lv_obj_t* list = lv_obj_get_parent(lv_event_get_target_obj(e));
    auto* ctx = static_cast<Context*>(lv_obj_get_user_data(list));
    ctx->favourites.toggle(manifest->id);
    // Deferred: populateList() deletes this button via lv_obj_clean() while its own key event is still dispatching.
    lv_async_call([](void* userData) {
        populateList(static_cast<lv_obj_t*>(userData));
    }, list);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onHelpPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    alertdialog::start(ctx->appInstanceId, "Help", "Long-press an app, or press F, to toggle it as a favorite.");
}

void createAppWidget(const ::AppManifest* manifest, lv_obj_t* list, bool favourite) {
    // Plain "*" prefix, not an icon: shared Material Symbols font is subsetted and has no star glyph.
    const std::string label = favourite ? (std::string("* ") + manifest->name) : manifest->name;
    lv_obj_t* btn = lv_list_add_button(list, LVGL_ICON_SHARED_TOOLBAR, label.c_str());
    lv_obj_t* image = lv_obj_get_child(btn, 0);
    lv_obj_set_style_text_font(image, lvgl_get_shared_icon_font(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, &onAppPressed, LV_EVENT_SHORT_CLICKED, const_cast<::AppManifest*>(manifest));
    lv_obj_add_event_cb(btn, &onAppLongPressed, LV_EVENT_LONG_PRESSED, const_cast<::AppManifest*>(manifest));
    lv_obj_add_event_cb(btn, &onAppKeyPressed, LV_EVENT_KEY, const_cast<::AppManifest*>(manifest));
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<const ::AppManifest*>*>(context);
    manifests->push_back(manifest);
}

void populateList(lv_obj_t* list) {
    lv_obj_clean(list);

    auto* ctx = static_cast<Context*>(lv_obj_get_user_data(list));
    const std::vector<std::string> favouriteIds = ctx->favourites.load();

    std::vector<const ::AppManifest*> manifests;
    app_manager_for_each_manifest(collectManifest, &manifests);
    std::ranges::sort(manifests, [&](const ::AppManifest* a, const ::AppManifest* b) {
        const bool aFavourite = Favourites::contains(favouriteIds, a->id);
        const bool bFavourite = Favourites::contains(favouriteIds, b->id);
        if (aFavourite != bFavourite) {
            return aFavourite;
        }
        return strcmp(a->name, b->name) < 0;
    });

    for (const auto* manifest: manifests) {
        bool is_valid_category = (manifest->category == APP_CATEGORY_USER) || (manifest->category == APP_CATEGORY_SYSTEM);
        if (is_valid_category && (manifest->flags & APP_MANIFEST_FLAG_HIDDEN) == 0) {
            createAppWidget(manifest, list, Favourites::contains(favouriteIds, manifest->id));
        }
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    // Flex column + flex_grow so the toolbar/list split recomputes on layout instead of going stale after a resize.
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Apps");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    lvgl_toolbar_add_text_button_action(toolbar, "?", onHelpPressed, ctx);

    lv_obj_t* list = lv_list_create(parent);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_user_data(list, ctx);

    populateList(list);
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
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT:
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
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
    .id = "tactility.applist",
    .name = "Apps",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4000, .desired_memory_capability = 0 },
};

} // namespace
