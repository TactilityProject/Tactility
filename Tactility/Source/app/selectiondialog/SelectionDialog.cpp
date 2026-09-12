#include <Tactility/app/selectiondialog/SelectionDialog.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::selectiondialog {

constexpr auto* TAG = "SelectionDialog";
constexpr auto* DEFAULT_TITLE = "Select...";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    // Set once in appMain() from its own argc/argv parameters, read by createWidgets() - see
    // AlertDialog.cpp's Context::argc/argv for why this is safe without a lock.
    int argc = 0;
    char** argv = nullptr;
    // The eventual appMain() return value - see AlertDialog.cpp's Context::result for why this
    // is a plain (non-atomic) field safely shared between the LVGL thread (writer, before
    // emitting APP_EVENT_CLOSE) and this dialog's own thread (reader, after waking from it).
    int32_t result = 1; // Cancelled - safety-net default if closed without selecting an item
};

struct ItemContext {
    Context* ctx;
    int32_t index;
};

void onItemDeleted(lv_event_t* e) {
    delete static_cast<ItemContext*>(lv_event_get_user_data(e));
}

void onItemSelected(lv_event_t* e) {
    auto* itemCtx = static_cast<ItemContext*>(lv_event_get_user_data(e));
    LOG_I(TAG, "Selected item at index %d", (int)itemCtx->index);
    itemCtx->ctx->result = itemCtx->index;
    app_event_emit_close(itemCtx->ctx->appInstanceId);
}

void createChoiceItem(Context* ctx, lv_obj_t* list, const std::string& title, int32_t index) {
    lv_obj_t* btn = lv_list_add_button(list, nullptr, title.c_str());
    auto* itemCtx = new ItemContext { ctx, index };
    lv_obj_add_event_cb(btn, onItemSelected, LV_EVENT_SHORT_CLICKED, itemCtx);
    lv_obj_add_event_cb(btn, onItemDeleted, LV_EVENT_DELETE, itemCtx);
}

// Closes the dialog immediately with a fixed result, without ever showing a choice list -
// mirrors the original's 0-items (error) and 1-item (auto-select) shortcuts.
void closeWithResult(Context* ctx, int32_t result) {
    ctx->result = result;
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    // argv layout: [0]=title, [1..argc)=items.
    int argc = ctx->argc;
    char** argv = ctx->argv;
    int itemCount = argc - 1;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    const char* title = (argv[0][0] != '\0') ? argv[0] : DEFAULT_TITLE;
    lvgl_toolbar_create(parent, title);

    auto* list = lv_list_create(parent);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);

    if (itemCount <= 0 || argv[1][0] == '\0') {
        LOG_E(TAG, "No items provided");
        closeWithResult(ctx, -1);
    } else if (itemCount == 1) {
        LOG_W(TAG, "Auto-selecting single item");
        closeWithResult(ctx, 0);
    } else {
        for (int32_t index = 0; index < itemCount; index++) {
            createChoiceItem(ctx, list, argv[1 + index], index);
        }
    }
}

int32_t appMain(int argc, char* argv[]) {
    AppInstanceId appInstanceId = app_scheduler_current_app_id();
    Context ctx { appInstanceId };
    ctx.argc = argc;
    ctx.argv = argv;

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

    return ctx.result;
}

} // namespace

namespace {

// Builds argv = [title, items...] for app_start_for_result().
std::vector<const char*> buildArgv(const std::string& title, const std::vector<std::string>& items) {
    std::vector<const char*> argv { title.c_str() };
    for (const auto& item: items) {
        argv.push_back(item.c_str());
    }
    return argv;
}

} // namespace

AppInstanceId start(AppInstanceId callerAppInstanceId, const std::string& title, const std::vector<std::string>& items) {
    auto argv = buildArgv(title, items);
    AppInstanceId instanceId = 0;
    app_start_for_result(manifest.id, static_cast<int>(argv.size()), argv.data(), callerAppInstanceId, &instanceId);
    return instanceId;
}

extern const AppManifest manifest = {
    .id = "tactility.selectiondialog",
    .name = "Selection Dialog",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

}
