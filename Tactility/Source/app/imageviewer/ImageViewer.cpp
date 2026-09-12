#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/StringUtils.h>
#include <tactility/check.h>
#include <tactility/log.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/widgets/toolbar.h>
#include <lvgl.h>

#include <string>

namespace tt::app::imageviewer {

extern const ::AppManifest manifest;

constexpr auto* TAG = "ImageViewer";

namespace {

struct Context {
    uint32_t appInstanceId;
    std::string filePath;
};


void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_size(wrapper, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_set_style_pad_gap(wrapper, 0, 0);

    auto* toolbar = lvgl_toolbar_create(wrapper, "Image Viewer");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    auto* image_wrapper = lv_obj_create(wrapper);
    lv_obj_align_to(image_wrapper, toolbar, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    lv_obj_set_width(image_wrapper, LV_PCT(100));
    auto parent_height = lv_obj_get_height(wrapper);
    auto toolbar_height = lv_obj_get_height(toolbar);
    lv_obj_set_height(image_wrapper, parent_height - toolbar_height);
    lv_obj_set_flex_flow(image_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(image_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(image_wrapper, 0, 0);
    lv_obj_set_style_pad_gap(image_wrapper, 0, 0);
    lvgl::obj_set_style_bg_invisible(image_wrapper);

    auto* image = lv_image_create(image_wrapper);
    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

    auto* file_label = lv_label_create(wrapper);
    lv_obj_align_to(file_label, wrapper, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if (!ctx->filePath.empty()) {
        std::string prefixed_path = lvgl::PATH_PREFIX + ctx->filePath;
        LOG_I(TAG, "Opening %s", prefixed_path.c_str());
        lv_img_set_src(image, prefixed_path.c_str());
        auto path = string::getLastPathSegment(ctx->filePath);
        lv_label_set_text(file_label, path.c_str());
    } else {
        lv_label_set_text(file_label, "File not found");
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    check(argc > 0, "Parameters not set");

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.filePath = argv[0];

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

void start(const std::string& file) {
    const char* argv[] = { file.c_str() };
    uint32_t instanceId = 0;
    app_start(manifest.id, 1, argv, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.imageviewer",
    .name = "Image Viewer",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
