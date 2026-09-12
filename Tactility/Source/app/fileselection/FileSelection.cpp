#include "Tactility/app/fileselection/FileSelection.h"
#include "Tactility/app/fileselection/FileSelectionPrivate.h"
#include "Tactility/app/fileselection/View.h"
#include "Tactility/app/fileselection/State.h"

#include <app/event.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>
#include <app/stream.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>

#include <cstdio>
#include <memory>
#include <string>
#include <unistd.h>

namespace tt::app::fileselection {

extern const ::AppManifest manifest;

constexpr auto* TAG = "FileSelection";

namespace {

struct Context {
    uint32_t appInstanceId;
    Mode mode;
    std::shared_ptr<State> state;
    std::unique_ptr<View> view;
    std::string resultPath;
    int32_t resultCode = 1; // 1 means Cancelled
};


void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->view->init(parent, ctx->mode);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    // argv layout: [0]="existing" or "existing_or_new".

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.mode = (argc > 0 && std::string(argv[0]) == "--existing-or-new") ? Mode::ExistingOrNew : Mode::Existing;
    ctx.state = std::make_shared<State>();
    ctx.view = std::make_unique<View>(appInstanceId, ctx.state, [&ctx, appInstanceId](const std::string& path) {
        ctx.resultPath = path;
        ctx.resultCode = 0;
        app_event_emit_close(appInstanceId);
    });

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

    if (ctx.resultCode == 0) {
        // The parent captures this via an AppStream bound to our stdout (see startWithMode()) -
        // see AppStdioWrap.cpp for how printf() itself gets routed there on POSIX.
        LOG_I(TAG, "Result: %s", ctx.resultPath.c_str());
        printf("%s", ctx.resultPath.c_str());
    }

    return ctx.resultCode;
}

} // namespace

namespace {

uint32_t startWithMode(const char* modeArg, uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup) {
    const char* argv[] = { modeArg };
    AppStreamBinding binding = {
        .producer_fd = STDOUT_FILENO,
        .stream = &stream,
        .buffer = buffer,
        .buffer_capacity = bufferCapacity,
        .event_group = eventGroup,
    };
    uint32_t instanceId = 0;
    app_start_for_result_with_streams(manifest.id, 1, argv, &binding, 1, callerAppInstanceId, &instanceId);
    return instanceId;
}

} // namespace

uint32_t startForExistingFile(uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup) {
    return startWithMode("--existing", callerAppInstanceId, stream, buffer, bufferCapacity, eventGroup);
}

uint32_t startForExistingOrNewFile(uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup) {
    return startWithMode("--existing-or-new", callerAppInstanceId, stream, buffer, bufferCapacity, eventGroup);
}

extern const ::AppManifest manifest = {
    .id = "tactility.fileselection",
    .name = "File Selection",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
