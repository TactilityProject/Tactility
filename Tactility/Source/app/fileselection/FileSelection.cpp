#include "Tactility/app/fileselection/FileSelection.h"
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
#include <vector>

namespace tt::app::fileselection {

extern const ::AppManifest manifest;

constexpr auto* TAG = "FileSelection";

namespace {

struct Context {
    uint32_t appInstanceId;
    std::shared_ptr<State> state;
    std::unique_ptr<View> view;
    std::string result;
    int32_t resultCode = 1; // 1 means Cancelled
};


void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->view->init(parent);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    // argv: flags "--new", "--folder", "--no-files", "--multiple" and "--ext=<extension>" (repeatable)
    Options options;
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--new") options.allowNewFile = true;
        else if (arg == "--folder") options.allowFolder = true;
        else if (arg == "--no-files") options.allowFiles = false;
        else if (arg == "--multiple") options.multiple = true;
        else if (arg.starts_with("--ext=")) options.extensions.push_back(arg.substr(6));
    }

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.state = std::make_shared<State>(std::move(options));
    ctx.view = std::make_unique<View>(appInstanceId, ctx.state, &event_group, [&ctx, appInstanceId](const std::vector<std::string>& paths) {
        ctx.result.clear();
        for (const auto& path : paths) {
            ctx.result += path;
            if (paths.size() > 1 || ctx.state->getOptions().multiple) {
                ctx.result += '\n';
            }
        }
        ctx.resultCode = 0;
        app_event_emit_close(appInstanceId);
    });

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    while (true) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if (flags & ctx.view->getFileOpsBit()) {
            ctx.view->onFileOpsCompleted();
        }

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
    // The view releases its event group bit when it is destroyed, so it ends before the event group does
    ctx.view.reset();
    task_event_group_destruct(&event_group);

    if (ctx.resultCode == 0) {
        // The parent captures this via an AppStream bound to our stdout (see startWithMode()) -
        // see Modules/app-posix-module/source/stdio_wrap.cpp for how printf() itself gets routed there on POSIX.
        LOG_I(TAG, "Result: %s", ctx.result.c_str());
        printf("%s", ctx.result.c_str());
    }

    return ctx.resultCode;
}

} // namespace

namespace {

} // namespace

uint32_t start(uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup, const Options& options) {
    std::vector<std::string> args;
    if (options.allowNewFile) args.emplace_back("--new");
    if (options.allowFolder) args.emplace_back("--folder");
    if (!options.allowFiles) args.emplace_back("--no-files");
    if (options.multiple) args.emplace_back("--multiple");
    for (const auto& extension : options.extensions) {
        args.push_back("--ext=" + extension);
    }
    std::vector<const char*> argv;
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }

    AppStreamBinding binding = {
        .producer_fd = STDOUT_FILENO,
        .stream = &stream,
        .buffer = buffer,
        .buffer_capacity = bufferCapacity,
        .event_group = eventGroup,
        .window_size = {},
        .alias_fd = -1,
    };
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_arguments_ext(&context, static_cast<int>(argv.size()), argv.data());
    app_start_context_set_streams(&context, &binding, 1);
    app_start_context_set_parent(&context, callerAppInstanceId);
    app_start_with_context(&context, &instanceId);
    return instanceId;
}

uint32_t startForExistingFile(uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup) {
    return start(callerAppInstanceId, stream, buffer, bufferCapacity, eventGroup, Options {});
}

uint32_t startForExistingOrNewFile(uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup) {
    return start(callerAppInstanceId, stream, buffer, bufferCapacity, eventGroup, Options { .allowNewFile = true });
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
