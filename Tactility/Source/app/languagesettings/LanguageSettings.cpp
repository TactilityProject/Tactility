#include <Tactility/settings/Language.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <localisation/tr.h>

#include <atomic>

namespace tt::app::languagesettings {

extern const ::AppManifest manifest;

namespace {

constexpr auto LANGUAGE_COUNT = static_cast<int32_t>(settings::Language::count);

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t selectBit = 0;
    /** The language that is set, read by the app task so widgets don't read files */
    std::atomic<int32_t> current = 0;
    std::atomic<int32_t> requested = -1;
    lv_obj_t* list = nullptr;
};

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onLanguagePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->requested = static_cast<int32_t>(lv_obj_get_index(lv_event_get_target_obj(event)));
    task_event_group_signal(ctx->eventGroup, ctx->selectBit);
}

/** Runs with the LVGL lock held */
void populateList(Context* ctx) {
    lv_obj_clean(ctx->list);
    for (int32_t i = 0; i < LANGUAGE_COUNT; i++) {
        const auto language = static_cast<settings::Language>(i);
        auto* button = lvgl_list_add_button(ctx->list, i == ctx->current ? LV_SYMBOL_OK : nullptr, settings::getDisplayName(language));
        lv_obj_add_event_cb(button, onLanguagePressed, LV_EVENT_SHORT_CLICKED, ctx);
    }
    lvgl_list_hide_last_divider(ctx->list);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Language"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    ctx->list = lvgl_list_create(parent);
    lv_obj_set_width(ctx->list, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->list, 1);
    populateList(ctx);
}

void destroyWidgets(void* userData) {
    static_cast<Context*>(userData)->list = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.current = static_cast<int32_t>(settings::getLanguage());

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.selectBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if (flags & ctx.selectBit) {
            const int32_t requested = ctx.requested.exchange(-1);
            if (requested >= 0 && requested < LANGUAGE_COUNT && requested != ctx.current) {
                // Saves the setting and reloads the catalogs, which read files
                if (settings::setLanguage(static_cast<settings::Language>(requested))) {
                    ctx.current = requested;
                    lvgl_lock();
                    if (ctx.list != nullptr) {
                        populateList(&ctx);
                    }
                    lvgl_unlock();
                }
            }
        }

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
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.languagesettings",
    .name = "Language",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::languagesettings
