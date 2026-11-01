#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/apphubdetails/AppHubDetailsApp.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <app_hub/app_hub.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <atomic>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

namespace tt::app::apphubdetails {

constexpr auto* TAG = "AppHubDetails";

extern const ::AppManifest manifest;

namespace {

constexpr auto* CONFIRM_TEXT = "Confirm";
constexpr auto* CANCEL_TEXT = "Cancel";
constexpr int32_t CONFIRMATION_BUTTON_INDEX = 0;
constexpr size_t DESCRIPTION_MAX_SIZE = 2048;

struct Context {
    uint32_t appInstanceId;
    tt::std::string appId;

    lv_obj_t* toolbar = nullptr;
    lv_obj_t* spinner = nullptr;
    lv_obj_t* updateButton = nullptr;
    lv_obj_t* updateLabel = nullptr;
    lv_obj_t* descriptionLabel = nullptr;
    lv_obj_t* versionLabel = nullptr;
    /** Whether createWidgets() ran before, so the next call is a resurface */
    bool created = false;

    // Read from the catalog file on this app's own thread (see loadState()), as the widget code
    // can't do file operations. Written with the LVGL lock held, read by the widget code.
    AppHubApp app {};
    tt::std::string description;

    // Set from the LVGL task (button press), read from this app's own thread (event loop) -
    // both directions cross threads, hence atomic.
    ::std::atomic<uint32_t> installDialogId = 0;
    ::std::atomic<uint32_t> uninstallDialogId = 0;
    ::std::atomic<uint32_t> updateDialogId = 0;

    TaskEventGroup* eventGroup = nullptr;
    uint32_t reloadBit = 0;
    /** Set when the window resurfaces: the catalog can change while a dialog covers it */
    ::std::atomic<bool> reloadRequested { false };
    AppHubEventSubscription appHubSub {};
};

AppHubApp readApp(Context* ctx) {
    AppHubApp app {};
    if (app_hub_get_app(ctx->appId.c_str(), &app) != ERROR_NONE) {
        strncpy(app.id, ctx->appId.c_str(), sizeof(app.id) - 1);
        strncpy(app.name, ctx->appId.c_str(), sizeof(app.name) - 1);
    }
    return app;
}

tt::std::string readDescription(Context* ctx) {
    tt::std::vector<char> buffer(DESCRIPTION_MAX_SIZE);
    if (app_hub_get_description(ctx->appId.c_str(), buffer.data(), buffer.size()) != ERROR_NONE || buffer[0] == '\0') {
        return "This app has no description yet.";
    }
    tt::std::string description = buffer.data();
    for (size_t pos = 0; (pos = description.find("\\n", pos)) != tt::std::string::npos;) {
        description.replace(pos, 2, "\n");
    }
    return description;
}

/** Reads the state from App Hub. Doesn't touch widgets, so it runs without the LVGL lock. */
void loadState(Context* ctx, bool includeDescription) {
    const auto app = readApp(ctx);
    tt::std::string description;
    if (includeDescription) {
        description = readDescription(ctx);
    }
    lvgl_lock();
    ctx->app = app;
    if (includeDescription) {
        ctx->description = ::std::move(description);
    }
    lvgl_unlock();
}

void updateViews(Context* ctx);

/** Loads the state and shows it. Runs on this app's own thread. */
void refreshViews(Context* ctx, bool includeDescription = false) {
    loadState(ctx, includeDescription);
    lvgl_lock();
    updateViews(ctx);
    lvgl_unlock();
}

uint32_t showConfirmDialog(Context* ctx, const char* action) {
    const auto message = ::std::format("{} {}?", action, ctx->app.name);
    return alertdialog::start(ctx->appInstanceId, CONFIRM_TEXT, message, ::std::vector<::std::string> { CONFIRM_TEXT, CANCEL_TEXT });
}

void onBackPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    app_event_emit_close(ctx->appInstanceId);
}

void onInstallPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->installDialogId = showConfirmDialog(ctx, "Install");
}

void onUninstallPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->uninstallDialogId = showConfirmDialog(ctx, "Uninstall");
}

void onUpdatePressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->updateDialogId = showConfirmDialog(ctx, "Update");
}

void queueOperation(Context* ctx, error_t (*operation)(const char* id, uint32_t* operation_id), const char* failure) {
    const error_t error = operation(ctx->appId.c_str(), nullptr);
    if (error == ERROR_RESOURCE_BUSY) {
        alertdialog::start(ctx->appInstanceId, "Busy", "All apps are being updated. Try again when it's done.");
        return;
    } else if (error != ERROR_NONE) {
        alertdialog::start(ctx->appInstanceId, "Error", failure);
        return;
    }
    refreshViews(ctx);
}

void onOperationEvent(Context* ctx, const AppHubEvent& event) {
    const bool finished = event.type == APP_HUB_EVENT_INSTALLED || event.type == APP_HUB_EVENT_REMOVED;
    if (finished && ctx->appId == event.app_id && event.result != ERROR_NONE) {
        LOG_E(TAG, "Operation failed: %s", error_to_string(event.result));
        const char* message = (event.type == APP_HUB_EVENT_REMOVED) ? "Failed to uninstall app" : "Failed to install app";
        alertdialog::start(ctx->appInstanceId, "Error", message);
    }
    refreshViews(ctx);
}

void updateViews(Context* ctx) {
    // Covered by another window (e.g. a dialog): createWidgets() updates the views when it's shown again
    if (ctx->toolbar == nullptr) {
        return;
    }
    lv_label_set_text(ctx->descriptionLabel, ctx->description.c_str());
    lv_label_set_text_fmt(ctx->versionLabel, "Version %s", ctx->app.in_catalog ? ctx->app.version_name : ctx->app.installed_version_name);
    lvgl_toolbar_clear_actions(ctx->toolbar);
    ctx->spinner = lvgl_toolbar_add_spinner_action(ctx->toolbar);
    lv_obj_set_hidden(ctx->updateLabel, true);

    // This app's install or removal continues after the window closes, so it can already be queued or running when the window opens.
    const auto& app = ctx->app;
    lv_obj_set_hidden(ctx->spinner, !app.operation_pending);
    if (app.operation_pending) {
        return;
    }

    if (app.installed) {
        if (app.update_available) {
            ctx->updateButton = lvgl_toolbar_add_image_button_action(ctx->toolbar, LV_SYMBOL_DOWNLOAD, onUpdatePressed, ctx);
            lv_obj_set_hidden(ctx->updateLabel, false);
        }
        lvgl_toolbar_add_image_button_action(ctx->toolbar, LV_SYMBOL_TRASH, onUninstallPressed, ctx);
    } else if (app.in_catalog) {
        lvgl_toolbar_add_image_button_action(ctx->toolbar, LV_SYMBOL_DOWNLOAD, onInstallPressed, ctx);
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    const auto& app = ctx->app;
    ctx->toolbar = lvgl_toolbar_create(parent, app.name);
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(ctx->toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

    ctx->updateLabel = lv_label_create(wrapper);
    lv_label_set_text(ctx->updateLabel, "Update available!");
    lv_obj_set_style_text_color(ctx->updateLabel, lv_color_make(0xff, 0xff, 00), LV_STATE_DEFAULT);

    ctx->descriptionLabel = lv_label_create(wrapper);
    lv_obj_set_width(ctx->descriptionLabel, LV_PCT(100));
    lv_label_set_long_mode(ctx->descriptionLabel, LV_LABEL_LONG_MODE_WRAP);

    ctx->versionLabel = lv_label_create(wrapper);

    updateViews(ctx);

    // The cached state shows right away. The app's task then reloads it, as the catalog can have
    // changed while a dialog covered this window.
    if (ctx->created) {
        ctx->reloadRequested = true;
        task_event_group_signal(ctx->eventGroup, ctx->reloadBit);
    }
    ctx->created = true;
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->descriptionLabel = nullptr;
    ctx->versionLabel = nullptr;
    ctx->toolbar = nullptr;
    ctx->spinner = nullptr;
    ctx->updateButton = nullptr;
    ctx->updateLabel = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    // argv layout: [0]=appId

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    if (argc >= 1) {
        ctx.appId = argv[0];
    }

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.reloadBit) == ERROR_NONE);
    check(app_hub_event_subscribe(&ctx.appHubSub, &event_group) == ERROR_NONE);

    loadState(&ctx, true);
    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT: {
                    bool confirmed = event.result.result == CONFIRMATION_BUTTON_INDEX;
                    if (event.result.launch_id == ctx.installDialogId && confirmed) {
                        queueOperation(&ctx, app_hub_install, "Failed to install app");
                    } else if (event.result.launch_id == ctx.uninstallDialogId && confirmed) {
                        queueOperation(&ctx, app_hub_remove, "Failed to uninstall app");
                    } else if (event.result.launch_id == ctx.updateDialogId && confirmed) {
                        queueOperation(&ctx, app_hub_install, "Failed to update app");
                    }
                    app_manager_stop(event.result.launch_id);
                    break;
                }
                default:
                    break;
            }
            if (shouldClose) break;
        }

        AppHubEvent app_hub_event {};
        while (!shouldClose && app_hub_event_poll(&ctx.appHubSub, &app_hub_event) == ERROR_NONE) {
            if (app_hub_event.type == APP_HUB_EVENT_REFRESHED) {
                // A new catalog can change the version, description and actions
                ctx.reloadRequested = true;
            } else if (app_hub_event.type != APP_HUB_EVENT_REFRESHING) {
                onOperationEvent(&ctx, app_hub_event);
            }
        }

        if (!shouldClose && ctx.reloadRequested.exchange(false)) {
            refreshViews(&ctx, true);
        }
    }

    // Queued operations continue in the background after the window closes
    window_manager_remove(window);
    check(app_hub_event_unsubscribe(&ctx.appHubSub) == ERROR_NONE);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

void start(const char* appId) {
    // Fire-and-forget (parent_instance_id 0): AppHub's own multi-app browsing list isn't
    // waiting on a result.
    const char* argv[] = { appId };
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_arguments_ext(&context, 1, argv);
    app_start_context_set_parent(&context, /*parent_instance_id=*/0);
    app_start_with_context(&context, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.apphubdetails",
    .name = "App Details",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
