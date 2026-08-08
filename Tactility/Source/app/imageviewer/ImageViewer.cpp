#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/StringUtils.h>
#include <tactility/check.h>
#include <tactility/log.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

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
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
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

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    check(argc > 0, "Parameters not set");

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.filePath = argv[0];

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

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

void start(const std::string& file) {
    const char* argv[] = { file.c_str() };
    uint32_t instanceId = 0;
    app_manager_start_with_parameters(manifest.id, 1, argv, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "ImageViewer",
    .name = "Image Viewer",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
