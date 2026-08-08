#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/LogMessages.h>
#include <Tactility/MountPoints.h>
#include <Tactility/Mutex.h>
#include <Tactility/StringUtils.h>
#include <Tactility/Timer.h>
#include <Tactility/settings/Time.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

#include <lvgl/lvgl.h>
#include <lvgl/icons/shared.h>
#include <lvgl/fonts.h>
#include <lvgl/widgets/toolbar.h>

#include <memory>

namespace tt::app::timezone {

constexpr auto* TAG = "TimeZone";

extern const ::AppManifest manifest;

namespace {

struct TimeZoneEntry {
    std::string name;
    std::string code;
};

struct Context {
    uint32_t appInstanceId;
    Mutex mutex;
    std::vector<TimeZoneEntry> entries;
    std::unique_ptr<Timer> updateTimer;
    lv_obj_t* listWidget = nullptr;
    lv_obj_t* filterTextareaWidget = nullptr;
    bool saveTimeZone = false;
    // The eventual appMain() return value - see AlertDialog.cpp's Context::result for why this
    // is a plain (non-atomic) field safely shared between the LVGL thread (writer, before
    // emitting APP_EVENT_CLOSE) and this app's own thread (reader, after waking from it).
    int32_t result = 1; // Cancelled - safety-net default if closed without picking a time zone
};


// The last picked name/code. Static rather than per-instance: simple, and in practice only one
// TimeZone dialog is ever open at a time. Written on the LVGL thread (the item-selected
// callback, before emitting APP_EVENT_CLOSE); read by the parent via getLastName()/getLastCode()
// after receiving that event - safe without a lock for the same reason Context::result is (see
// AlertDialog.cpp).
std::string lastName;
std::string lastCode;

bool parseEntry(const std::string& input, std::string& outName, std::string& outCode) {
    std::string partial_strip = input.substr(1, input.size() - 3);
    auto first_end_quote = partial_strip.find('"');
    if (first_end_quote == std::string::npos) {
        return false;
    } else {
        outName = partial_strip.substr(0, first_end_quote);
        outCode = partial_strip.substr(first_end_quote + 3);
        return true;
    }
}

void onTextareaValueChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        if (ctx->updateTimer->isRunning()) {
            ctx->updateTimer->stop();
        }
        ctx->updateTimer->start();
        ctx->mutex.unlock();
    }
}

void createListItem(Context* ctx, lv_obj_t* list, const std::string& title, size_t index) {
    auto* btn = lv_list_add_button(list, nullptr, title.c_str());
    struct ButtonContext {
        Context* ctx;
        size_t index;
    };
    auto* buttonCtx = new ButtonContext { ctx, index };
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* buttonCtx = static_cast<ButtonContext*>(lv_event_get_user_data(e));
        delete buttonCtx;
    }, LV_EVENT_DELETE, buttonCtx);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* buttonCtx = static_cast<ButtonContext*>(lv_event_get_user_data(e));
        auto* ctx = buttonCtx->ctx;
        auto index = buttonCtx->index;
        LOG_I(TAG, "Selected item at index %d", (int)index);

        auto& entry = ctx->entries[index];

        if (ctx->saveTimeZone) {
            settings::setTimeZone(entry.name, entry.code);
        }

        lastName = entry.name;
        lastCode = entry.code;

        ctx->result = 0; // Ok
        AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
        app_event_emit(ctx->appInstanceId, &closeEvent);
    }, LV_EVENT_SHORT_CLICKED, buttonCtx);
}

void readTimeZones(Context* ctx, std::string filter) {
    auto path = std::string(file::MOUNT_POINT_SYSTEM) + "/timezones.csv";
    auto* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open %s", path.c_str());
        return;
    }
    char line[96];
    std::string name;
    std::string code;
    uint32_t count = 0;
    std::vector<TimeZoneEntry> new_entries;
    while (fgets(line, 96, file)) {
        if (parseEntry(line, name, code)) {
            if (string::lowercase(name).find(filter) != std::string::npos) {
                count++;
                new_entries.push_back({.name = name, .code = code});

                // Safety guard
                if (count > 50) {
                    // TODO: Show warning that we're not displaying a complete list
                    break;
                }
            }
        } else {
            LOG_E(TAG, "Parse error at line %llu", count);
        }
    }

    fclose(file);

    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        ctx->entries = std::move(new_entries);
        ctx->mutex.unlock();
    } else {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
    }

    LOG_I(TAG, "Processed %llu entries", count);
}

void updateList(Context* ctx) {
    if (lvgl_try_lock(200 / portTICK_PERIOD_MS)) {
        std::string filter = string::lowercase(std::string(lv_textarea_get_text(ctx->filterTextareaWidget)));
        lvgl_unlock();
        readTimeZones(ctx, filter);
    } else {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "TimeZone LVGL");
        return;
    }

    if (lvgl_try_lock(200 / portTICK_PERIOD_MS)) {
        if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
            lv_obj_clean(ctx->listWidget);

            uint32_t index = 0;
            for (auto& entry : ctx->entries) {
                createListItem(ctx, ctx->listWidget, entry.name, index);
                index++;
            }

            ctx->mutex.unlock();
        }

        lvgl_unlock();
    } else {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "TimeZone LVGL");
    }
}

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

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Select Time zone");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* search_wrapper = lv_obj_create(parent);
    lv_obj_set_size(search_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(search_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(search_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(search_wrapper, 0, 0);
    lv_obj_set_style_border_width(search_wrapper, 0, 0);

    auto* icon = lv_image_create(search_wrapper);
    lv_obj_set_style_margin_left(icon, 8, 0);
    lv_obj_set_style_image_recolor_opa(icon, 255, 0);
    lv_obj_set_style_image_recolor(icon, lv_theme_get_color_primary(parent), 0);
    lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_font(), LV_STATE_DEFAULT);
    lv_image_set_src(icon, LVGL_ICON_SHARED_SEARCH);

    auto* textarea = lv_textarea_create(search_wrapper);
    lv_textarea_set_placeholder_text(textarea, "e.g. Europe/Amsterdam");
    lv_textarea_set_one_line(textarea, true);
    lv_obj_add_event_cb(textarea, onTextareaValueChanged, LV_EVENT_VALUE_CHANGED, ctx);
    ctx->filterTextareaWidget = textarea;
    lv_obj_set_flex_grow(textarea, 1);

    auto* list = lv_list_create(parent);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_border_width(list, 0, 0);
    ctx->listWidget = list;
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    // argv layout: [0]="1"/"0" (saveTimeZone).

    Context ctx;
    ctx.appInstanceId = appInstanceId;
    ctx.saveTimeZone = argc > 0 && argv[0][0] == '1';

    ctx.updateTimer = std::make_unique<Timer>(Timer::Type::Once, 500 / portTICK_PERIOD_MS, [&ctx] {
        updateList(&ctx);
    });

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);
    ctx.updateTimer->start();

    while (true) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        if (event.type == APP_EVENT_CLOSE) {
            app_manager_finish(appInstanceId); // no-op: modal children never supersede anything
            break;
        }
    }

    ctx.updateTimer->stop();
    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return ctx.result;
}

} // namespace

uint32_t start(uint32_t callerAppInstanceId, bool saveTimeZone) {
    const char* argv[] = { saveTimeZone ? "1" : "0" };
    uint32_t instanceId = 0;
    app_manager_start_for_result(manifest.id, callerAppInstanceId, 1, argv, &instanceId);
    return instanceId;
}

std::string getLastName() {
    return lastName;
}

std::string getLastCode() {
    return lastCode;
}

extern const ::AppManifest manifest = {
    .id = "TimeZone",
    .name = "Select Time zone",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

}
