#include <lvgl/icons/shared.h>
#include <lvgl/fonts.h>

#include <Tactility/app/apppackagedetails/AppPackageDetails.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/package_manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>

#include <TactilityCpp/Allocator.h>

#include <lvgl/widgets/toolbar.h>
#include <lvgl.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace tt::app::apppackagelist {

extern const ::AppManifest manifest;

namespace {

// Prefers PSRAM, like the other app list buffers
using PackageManifestList = std::vector<PackageManifest, OptExternalAllocator<PackageManifest>>;

struct Context {
    uint32_t appInstanceId;
    // Must outlive the widgets - button user-data points into this, not a createWidgets()-local vector.
    PackageManifestList packages = {};
};

void onPackagePressed(lv_event_t* e) {
    const auto* package = static_cast<const PackageManifest*>(lv_event_get_user_data(e));
    apppackagedetails::start(package->id);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createPackageWidget(const PackageManifest* package, lv_obj_t* list) {
    // A v2 package's single app shares its id, and its name is the nicer label. Otherwise show the package id.
    AppManifest appManifest;
    const char* label = (app_manager_find_manifest(package->id, &appManifest) == ERROR_NONE) ? appManifest.name : package->id;
    lv_obj_t* btn = lv_list_add_button(list, LVGL_ICON_SHARED_DEPLOYED_CODE, label);
    lv_obj_t* image = lv_obj_get_child(btn, 0);
    lv_obj_set_style_text_font(image, lvgl_get_shared_icon_default_font(), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, &onPackagePressed, LV_EVENT_SHORT_CLICKED, const_cast<PackageManifest*>(package));
}

void collectPackage(const ::AppPackage* pkg, void* context) {
    auto* packages = static_cast<PackageManifestList*>(context);
    packages->push_back(pkg->package);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    // Flex column + flex_grow; see AppList.cpp's createWidgets() for why a fixed height computed
    // once from lv_obj_get_content_height(parent) goes stale.
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Installed Apps");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    lv_obj_t* list = lv_list_create(parent);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);

    // createWidgets() can rerun for this same Context (window rebuild-on-remove).
    ctx->packages.clear();
    app_manager_for_each_package(collectPackage, &ctx->packages);
    std::ranges::sort(ctx->packages, [](const PackageManifest& left, const PackageManifest& right) {
        return strcmp(left.id, right.id) < 0;
    });

    for (const auto& package : ctx->packages) {
        createPackageWidget(&package, list);
    }

    if (ctx->packages.empty()) {
        // lv_obj_align() is ignored for children of a flex-managed parent, so the empty-state
        // label needs its own flex-growing wrapper to center within; the (empty) list is hidden
        // rather than deleted so the wrapper can just take its place in the flex flow.
        lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_flex_grow(list, 0);

        auto* empty_wrapper = lv_obj_create(parent);
        lv_obj_set_width(empty_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(empty_wrapper, 1);
        lv_obj_set_flex_align(empty_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(empty_wrapper, 0, LV_STATE_DEFAULT);

        auto* no_apps_label = lv_label_create(empty_wrapper);
        lv_label_set_text(no_apps_label, "No apps installed");
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

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.apppackagelist",
    .name = "Apps",
    .category = APP_CATEGORY_SETTINGS,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = { .depth = 3072, .desired_memory_capability = 0 },
};

} // namespace
