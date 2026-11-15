#define L10N_APP "kernelsettings"
#include <Tactility/i18n/AppDomain.h>
#include <app/event.h>
#include <app/install.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/error.h>
#include <tactility/module.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <atomic>
#include <string>
#include <vector>

namespace tt::app::kernelsettings {

extern const ::AppManifest manifest;

namespace {

enum class Failure { None, StartFailed, StopFailed };

struct Entry {
    std::string packageId;
    std::string name;
    bool started = false;
    Failure failure = Failure::None;
    error_t error = ERROR_NONE;
};

struct Context {
    uint32_t appInstanceId = 0;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t toggleBit = 0;
    // Set by the buttons and applied by appMain, because starting and stopping modules can take the LVGL lock
    std::atomic<int32_t> toggleIndex { -1 };

    // Only appMain writes this, and only with the LVGL lock held
    std::vector<Entry> entries;

    // nullptr while the widgets don't exist
    lv_obj_t* list = nullptr;
};

void collectEntry(const AppModuleInfo* info, void* context) {
    auto* entries = static_cast<std::vector<Entry>*>(context);
    entries->push_back({ info->package_id, info->module->name, module_is_started(info->module) });
}

std::string statusText(const Entry& entry) {
    switch (entry.failure) {
        case Failure::StartFailed:
            return i18n::format(tr("Failed to start: {}"), { error_to_string(entry.error) });
        case Failure::StopFailed:
            return i18n::format(tr("Failed to stop: {}"), { error_to_string(entry.error) });
        case Failure::None:
            break;
    }
    return entry.started ? tr("Started") : tr("Stopped");
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onToggle(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->toggleIndex = static_cast<int32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)));
    task_event_group_signal(ctx->eventGroup, ctx->toggleBit);
}

lv_obj_t* createTransparentContainer(lv_obj_t* parent) {
    auto* container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(container, false);
    return container;
}

void createRow(lv_obj_t* parent, Context* ctx, size_t index) {
    const auto& entry = ctx->entries[index];

    auto* row = createTransparentContainer(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* text = createTransparentContainer(row);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);

    auto* nameLabel = lv_label_create(text);
    lv_label_set_text(nameLabel, entry.name.c_str());
    lv_obj_set_width(nameLabel, LV_PCT(100));
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    auto* statusLabel = lv_label_create(text);
    lv_label_set_text(statusLabel, statusText(entry).c_str());
    lv_obj_set_width(statusLabel, LV_PCT(100));
    lv_obj_set_style_text_font(statusLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    auto* button = lv_button_create(row);
    lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
    lv_obj_add_event_cb(button, onToggle, LV_EVENT_SHORT_CLICKED, ctx);
    auto* buttonLabel = lv_label_create(button);
    lv_label_set_text(buttonLabel, entry.started ? tr("Stop") : tr("Start"));
}

// Must be called with the LVGL lock held
void populate(Context* ctx) {
    if (ctx->list == nullptr) {
        return;
    }

    lv_obj_clean(ctx->list);

    if (ctx->entries.empty()) {
        auto* label = lv_label_create(ctx->list);
        lv_label_set_text(label, tr("No installed modules"));
        return;
    }

    for (size_t i = 0; i < ctx->entries.size(); i++) {
        createRow(ctx->list, ctx, i);
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Kernel"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);

    auto* title = lv_label_create(wrapper);
    lv_label_set_text(title, tr("Modules"));

    auto* card = lvgl_card_create(wrapper);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, LV_DPX(8), LV_STATE_DEFAULT);
    ctx->list = card;

    populate(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->list = nullptr;
}

// Reads the installed modules again and keeps the failures of the entries that are still there
void reload(Context* ctx, const Entry* failedEntry) {
    std::vector<Entry> entries;
    app_module_for_each(collectEntry, &entries);

    for (auto& entry : entries) {
        const Entry* previous = nullptr;
        if (failedEntry != nullptr && failedEntry->packageId == entry.packageId && failedEntry->name == entry.name) {
            previous = failedEntry;
        } else {
            for (const auto& old : ctx->entries) {
                if (old.packageId == entry.packageId && old.name == entry.name) {
                    previous = &old;
                    break;
                }
            }
        }
        if (previous != nullptr) {
            entry.failure = previous->failure;
            entry.error = previous->error;
        }
    }

    lvgl_lock();
    ctx->entries = std::move(entries);
    populate(ctx);
    lvgl_unlock();
}

void toggle(Context* ctx, int32_t index) {
    if (index < 0 || static_cast<size_t>(index) >= ctx->entries.size()) {
        return;
    }

    Entry result = ctx->entries[index];

    // The loader does the lookup and the start or stop under its lock, because an uninstall unloads the module's binary
    const bool start = !result.started;
    result.error = app_module_set_started(result.packageId.c_str(), result.name.c_str(), start);
    if (result.error == ERROR_NOT_FOUND) {
        // The package was uninstalled in the meantime
        reload(ctx, nullptr);
        return;
    }

    if (result.error == ERROR_NONE) {
        result.failure = Failure::None;
    } else {
        result.failure = start ? Failure::StartFailed : Failure::StopFailed;
    }

    reload(ctx, &result);
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.toggleBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    app_module_for_each(collectEntry, &ctx.entries);

    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        toggle(&ctx, ctx.toggleIndex.exchange(-1));

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_release_bit(&event_group, ctx.toggleBit);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.kernelsettings",
    .name = "Kernel",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace
