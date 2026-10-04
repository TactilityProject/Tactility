#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/package_manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <Tactility/app/AppGrid.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/applist/Favourites.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::applist {

constexpr auto* TAG = "AppList";

namespace {

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

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<::AppManifest>*>(context);
    manifests->push_back(*manifest);
}

// Apps from installed packages this device can't run (wrong device, too little RAM)
void collectIncompatibleAppIds(const AppPackage* package, void* context) {
    if (app_package_manifest_is_compatible(&package->package)) {
        return;
    }
    auto* ids = static_cast<std::vector<std::string>*>(context);
    for (size_t i = 0; i < package->app_id_count; i++) {
        ids->emplace_back(package->app_ids[i]);
    }
}

std::vector<AppGridItem> collectItems(void* userData);
void onAppClicked(const ::AppManifest& manifest, void* userData);
void onAppLongPressed(const ::AppManifest& manifest, void* userData);
void onAppKey(const ::AppManifest& manifest, uint32_t key, void* userData);

struct Context {
    uint32_t appInstanceId;
    Favourites favourites;
    AppGrid grid { AppGrid::Callbacks {
        .collect = collectItems,
        .onClicked = onAppClicked,
        .onLongPressed = onAppLongPressed,
        .onKey = onAppKey,
        .userData = this
    } };
};

std::vector<AppGridItem> collectItems(void* userData) {
    const auto* ctx = static_cast<Context*>(userData);
    const std::vector<std::string> favouriteIds = ctx->favourites.load();

    std::vector<::AppManifest> collected;
    app_manager_for_each_manifest(collectManifest, &collected);
    std::vector<std::string> incompatibleIds;
    app_manager_for_each_package(collectIncompatibleAppIds, &incompatibleIds);
    std::erase_if(collected, [&](const ::AppManifest& manifest) {
        const bool is_valid_category = (manifest.category == APP_CATEGORY_USER) || (manifest.category == APP_CATEGORY_SYSTEM);
        if (!is_valid_category || (manifest.flags & APP_MANIFEST_FLAG_HIDDEN) != 0) {
            return true;
        }
        return std::ranges::find(incompatibleIds, std::string(manifest.id)) != incompatibleIds.end();
    });

    std::vector<AppGridItem> items;
    items.reserve(collected.size());
    for (const auto& manifest : collected) {
        items.push_back({ manifest, appIcon(&manifest), Favourites::contains(favouriteIds, manifest.id) });
    }
    std::ranges::sort(items, [](const AppGridItem& a, const AppGridItem& b) {
        if (a.highlighted != b.highlighted) {
            return a.highlighted;
        }
        return strcmp(a.manifest.name, b.manifest.name) < 0;
    });
    return items;
}

void onAppClicked(const ::AppManifest& manifest, void* userData) {
    const auto* ctx = static_cast<Context*>(userData);
    uint32_t instanceId = 0;
    AppStartContext context;
    if (app_start_context_from_id(manifest.id, &context) == ERROR_NONE && app_start_with_context(&context, &instanceId) == ERROR_NONE) {
        app_event_emit_close(ctx->appInstanceId);
    }
}

void toggleFavourite(Context* ctx, const char* appId, bool keepSelection) {
    if (!ctx->favourites.toggle(appId)) {
        LOG_E(TAG, "Failed to save favourites");
    }
    ctx->grid.requestRepopulate(keepSelection);
}

void onAppLongPressed(const ::AppManifest& manifest, void* userData) {
    toggleFavourite(static_cast<Context*>(userData), manifest.id, false);
}

void onAppKey(const ::AppManifest& manifest, uint32_t key, void* userData) {
    if (key == 'f' || key == 'F') {
        toggleFavourite(static_cast<Context*>(userData), manifest.id, true);
    }
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onHelpPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    alertdialog::start(ctx->appInstanceId, "Help", "Long-press an app, or press F, to toggle it as a favorite.");
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    // Flex column + flex_grow so the toolbar/grid split recomputes on layout instead of going stale after a resize.
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Apps");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    ctx->grid.createWidgets(parent, toolbar);
    auto* help_button = lvgl_toolbar_add_text_button_action(toolbar, LVGL_ICON_SHARED_HELP, onHelpPressed, ctx);
    lv_obj_set_style_text_font(help_button, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
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
    .stack = { .depth = 5120, .desired_memory_capability = 0 },
};

} // namespace
