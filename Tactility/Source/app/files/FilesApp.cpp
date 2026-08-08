#include <Tactility/app/files/View.h>
#include <Tactility/app/files/State.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <memory>

namespace tt::app::files {

extern const ::AppManifest manifest;

namespace {

struct CreateContext {
    View* view;
    uint32_t appInstanceId;
};

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<CreateContext*>(userData);
    ctx->view->init(ctx->appInstanceId, parent);
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    auto state = std::make_shared<State>();
    View view(state);
    CreateContext createContext { &view, appInstanceId };

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &createContext);

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
            case APP_EVENT_RESULT:
                view.onResult(event.result.launch_id, event.result.result);
                app_manager_stop(event.result.launch_id);
                break;
            default:
                break;
        }
    }

    view.deinit();
    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "Files",
    .name = "Files",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
