#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/applist/Favourites.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include <lvgl/icons/shared.h>
#include <lvgl/fonts.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::applist {

constexpr auto* TAG = "AppList";

namespace {

struct Context {
    uint32_t appInstanceId;
    Favourites favourites;
    std::vector<::AppManifest> manifests;
};

struct IconEntry {
    const char* id;
    const char* icon;
};

constexpr IconEntry ICONS[] = {
    {"tactility.apphub",            LVGL_ICON_SHARED_DOWNLOAD},
    {"tactility.camera",            LVGL_ICON_SHARED_CAMERA},
    {"tactility.chat",              LVGL_ICON_SHARED_FORUM},
    {"tactility.i2cscanner",        LVGL_ICON_SHARED_CABLE},
    {"tactility.notes",             LVGL_ICON_SHARED_EDIT_NOTE},
    {"tactility.screenshot",        LVGL_ICON_SHARED_IMAGE},
    {"tactility.systeminfo",        LVGL_ICON_SHARED_DEVICES},
    {"tactility.terminal",          LVGL_ICON_SHARED_TERMINAL},
    {"tactility.webserversettings", LVGL_ICON_SHARED_CLOUD},
};

const char* appIcon(const ::AppManifest* manifest) {
    for (const auto& entry : ICONS) {
        if (!strcmp(manifest->id, entry.id)) return entry.icon;
    }
    return LVGL_ICON_SHARED_DEPLOYED_CODE;
}

void populateList(lv_obj_t* list);

// Named (not a lambda) so lv_async_call_cancel() below has a stable function pointer to match against.
void deferredRepopulate(void* userData) {
    populateList(static_cast<lv_obj_t*>(userData));
}

void onListDeleted(lv_event_t* e) {
    // Cancels a still-pending deferredRepopulate() scheduled by a long-press/key-press right
    // before the list itself was deleted (e.g. the app closing)
    // Otherwise it would run later against this now-dangling lv_obj_t*.
    lv_async_call_cancel(deferredRepopulate, lv_event_get_target_obj(e));
}

void onAppPressed(lv_event_t* e) {
    const auto* manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    uint32_t instanceId = 0;
    AppStartContext context;
    if (app_start_context_from_id(manifest->id, &context) == ERROR_NONE && app_start_with_context(&context, &instanceId) == ERROR_NONE) {
        lv_obj_t* list = lv_obj_get_parent(lv_event_get_target_obj(e));
        auto* ctx = static_cast<Context*>(lv_obj_get_user_data(list));
        app_event_emit_close(ctx->appInstanceId);
    }
}

void onAppLongPressed(lv_event_t* e) {
    const auto* manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    lv_obj_t* list = lv_obj_get_parent(lv_event_get_target_obj(e));
    auto* ctx = static_cast<Context*>(lv_obj_get_user_data(list));
    if (!ctx->favourites.toggle(manifest->id)) {
        LOG_E(TAG, "Failed to save favourites");
    }
    // Deferred: populateList() deletes this button via lv_obj_clean() while its own long-press is still dispatching.
    lv_async_call(deferredRepopulate, list);
}

void onAppKeyPressed(lv_event_t* e) {
    const uint32_t key = lv_event_get_key(e);
    if (key != 'f' && key != 'F') {
        return;
    }
    const auto* manifest = static_cast<const ::AppManifest*>(lv_event_get_user_data(e));
    lv_obj_t* list = lv_obj_get_parent(lv_event_get_target_obj(e));
    auto* ctx = static_cast<Context*>(lv_obj_get_user_data(list));
    if (!ctx->favourites.toggle(manifest->id)) {
        LOG_E(TAG, "Failed to save favourites");
    }
    // Deferred: populateList() deletes this button via lv_obj_clean() while its own key event is still dispatching.
    lv_async_call(deferredRepopulate, list);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onHelpPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    alertdialog::start(ctx->appInstanceId, "Help", "Long-press an app, or press F, to toggle it as a favorite.");
}

lv_obj_t* createAppWidget(const ::AppManifest* manifest, lv_obj_t* list, bool favourite) {
    // Plain "*" prefix, not an icon: shared Material Symbols font is subsetted and has no star glyph.
    const std::string label = favourite ? (std::string("* ") + manifest->name) : manifest->name;
    lv_obj_t* btn = lv_list_add_button(list, appIcon(manifest), label.c_str());
    lv_obj_t* image = lv_obj_get_child(btn, 0);
    lv_obj_set_style_text_font(image, lvgl_get_shared_icon_font(), LV_PART_MAIN);
    // Not read by any event here (those get the manifest via their own callback user data) - lets
    // populateList() identify a button by app id after a rebuild, to restore focus onto it.
    lv_obj_set_user_data(btn, const_cast<::AppManifest*>(manifest));
    lv_obj_add_event_cb(btn, &onAppPressed, LV_EVENT_SHORT_CLICKED, const_cast<::AppManifest*>(manifest));
    lv_obj_add_event_cb(btn, &onAppLongPressed, LV_EVENT_LONG_PRESSED, const_cast<::AppManifest*>(manifest));
    lv_obj_add_event_cb(btn, &onAppKeyPressed, LV_EVENT_KEY, const_cast<::AppManifest*>(manifest));
    return btn;
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<::AppManifest>*>(context);
    manifests->push_back(*manifest);
}

void populateList(lv_obj_t* list) {
    // Captured before lv_obj_clean() deletes the currently focused button below: LVGL moves
    // focus elsewhere as it leaves the group, and creating replacement buttons doesn't restore
    // it, so the app id is remembered here and re-focused on its new button once rebuilt.
    std::string focusedAppId;
    lv_group_t* group = lv_group_get_default();
    if (group != nullptr) {
        lv_obj_t* focused = lv_group_get_focused(group);
        if (focused != nullptr && lv_obj_get_parent(focused) == list) {
            const auto* focusedManifest = static_cast<const ::AppManifest*>(lv_obj_get_user_data(focused));
            if (focusedManifest != nullptr) {
                focusedAppId = focusedManifest->id;
            }
        }
    }

    lv_obj_clean(list);

    auto* ctx = static_cast<Context*>(lv_obj_get_user_data(list));
    const std::vector<std::string> favouriteIds = ctx->favourites.load();

    std::vector<::AppManifest> collected;
    app_manager_for_each_manifest(collectManifest, &collected);
    std::ranges::sort(collected, [&](const ::AppManifest& a, const ::AppManifest& b) {
        const bool aFavourite = Favourites::contains(favouriteIds, a.id);
        const bool bFavourite = Favourites::contains(favouriteIds, b.id);
        if (aFavourite != bFavourite) {
            return aFavourite;
        }
        return strcmp(a.name, b.name) < 0;
    });
    // Old buttons (and every pointer into the previous ctx->manifests) are already gone via
    // lv_obj_clean() above, so this is safe to replace wholesale before building new ones.
    ctx->manifests = std::move(collected);

    lv_obj_t* focusedButton = nullptr;
    for (const auto& manifest: ctx->manifests) {
        bool is_valid_category = (manifest.category == APP_CATEGORY_USER) || (manifest.category == APP_CATEGORY_SYSTEM);
        if (is_valid_category && (manifest.flags & APP_MANIFEST_FLAG_HIDDEN) == 0) {
            lv_obj_t* btn = createAppWidget(&manifest, list, Favourites::contains(favouriteIds, manifest.id));
            if (!focusedAppId.empty() && focusedAppId == manifest.id) {
                focusedButton = btn;
            }
        }
    }

    if (focusedButton != nullptr) {
        lv_group_focus_obj(focusedButton);
        lv_obj_add_state(focusedButton, LV_STATE_FOCUS_KEY);
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
    lv_obj_add_event_cb(list, &onListDeleted, LV_EVENT_DELETE, nullptr);

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
