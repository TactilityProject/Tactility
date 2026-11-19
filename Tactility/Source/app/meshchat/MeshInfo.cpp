#define L10N_APP "mesh"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/meshchat/MeshChatCommon.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/std/vector.hpp>
#include <tactility/std/string.hpp>

#include <mesh/mesh.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <string>

namespace tt::app::meshchat {

extern const ::AppManifest infoManifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    /** One line per fact. Filled by the app's task, shown by updateWidgets(). */
    tt::std::vector<tt::std::string> lines;
    // Valid while the window is shown
    lv_obj_t* content = nullptr;
};

/** Queries the service thread, so it's called without the LVGL lock */
void buildLines(tt::std::vector<tt::std::string>& lines) {
    MeshInfo info;
    if (mesh_get_info(&info) != ERROR_NONE) {
        lines.push_back(getNotReadyText().c_str());
        return;
    }
    using i18n::format;
    auto add = [&lines](const ::std::string& line) { lines.push_back(line.c_str()); };
    add(format(tr("Status: {}"), { mesh_state_to_string(info.state) }));
    add(format(tr("Network: {}"), { info.backend }));
    add(format(tr("Region: {}"), { radio_region_to_string(info.region) }));
    if (info.radio.frequency > 0) {
        // Integer math, because printing floats needs a lot of stack
        const ::std::string frequency = ::std::to_string(info.radio.frequency / 1000000) + "." + ::std::to_string(1000 + (info.radio.frequency / 1000) % 1000).substr(1);
        const ::std::string bandwidth = ::std::to_string(info.radio.bandwidth / 1000) + ((info.radio.bandwidth % 1000) != 0 ? "." + ::std::to_string(10 + (info.radio.bandwidth % 1000) / 100).substr(1) : "");
        add(format(tr("Radio: {} MHz, {} kHz, SF{}, CR 4/{}, {} dBm"), {
            frequency, bandwidth, ::std::to_string(info.radio.spreading_factor), ::std::to_string(info.radio.coding_rate), ::std::to_string(info.radio.power)
        }));
    }
    if (info.airtime_budget_ms == 0) {
        add(tr("Duty cycle: unrestricted"));
    } else {
        add(format(tr("Duty cycle: {} s of {} s used ({}% allowed)"), {
            ::std::to_string((info.airtime_used_ms + 500) / 1000), ::std::to_string(info.airtime_budget_ms / 1000), ::std::to_string(info.duty_cycle_percent)
        }));
        if (info.duty_cycle_blocked) {
            add(tr("Transmitting is paused until the budget frees up"));
        }
    }
    add(format(tr("Waiting to transmit: {}"), { ::std::to_string(info.tx_queue_length) }));
    add(format(tr("Frames: {} sent, {} failed, {} received, {} unreadable"), {
        ::std::to_string(info.frames_sent), ::std::to_string(info.frames_failed), ::std::to_string(info.frames_received), ::std::to_string(info.frames_unreadable)
    }));
    add(format(tr("Messages: {} received, {} sent, {} failed"), {
        ::std::to_string(info.messages_received), ::std::to_string(info.messages_sent), ::std::to_string(info.messages_failed)
    }));
    add(format(tr("Nodes heard: {}, contacts: {}"), { ::std::to_string(info.nodes_heard), ::std::to_string(info.contacts) }));
}

/** The lines are built first, as the window can be rebuilt on another thread while this one reads the service */
void loadLines(Context* ctx) {
    tt::std::vector<tt::std::string> lines;
    buildLines(lines);
    lvgl_lock();
    ctx->lines = ::std::move(lines);
    lvgl_unlock();
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

/** Requires the LVGL lock */
void updateWidgets(Context* ctx) {
    if (ctx->content == nullptr) {
        return;
    }
    const int32_t scrollY = lv_obj_get_scroll_y(ctx->content);
    lv_obj_clean(ctx->content);
    auto* list = createCard(ctx->content);
    for (const auto& line : ctx->lines) {
        lvgl_list_add_text(list, line.c_str());
    }
    lvgl_list_hide_last_divider(list);
    // The rebuilt list starts at the top
    lv_obj_update_layout(ctx->content);
    lv_obj_scroll_to_y(ctx->content, scrollY, LV_ANIM_OFF);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Info"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_LEFT, onBackPressed, ctx);

    ctx->content = createContent(parent);
    updateWidgets(ctx);
}

void destroyWidgets(void* userData) {
    static_cast<Context*>(userData)->content = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);
    MeshEventSubscription meshEvents {};
    check(mesh_event_subscribe(&meshEvents, &eventGroup) == ERROR_NONE);

    loadLines(&ctx);
    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&eventGroup, nullptr, portMAX_DELAY);

        // Any event can change the counters
        MeshEvent meshEvent {};
        bool changed = false;
        while (mesh_event_poll(&meshEvents, &meshEvent) == ERROR_NONE) {
            changed = true;
        }

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }

        if (changed && !shouldClose) {
            loadLines(&ctx);
            lvgl_lock();
            updateWidgets(&ctx);
            lvgl_unlock();
        }
    }

    window_manager_remove(window);
    check(mesh_event_unsubscribe(&meshEvents) == ERROR_NONE);
    check(app_event_unsubscribe(&appEvents) == ERROR_NONE);
    task_event_group_destruct(&eventGroup);
    return 0;
}

} // namespace

extern const ::AppManifest infoManifest = {
    .id = MESH_INFO_APP_ID,
    .name = "Mesh Info",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace tt::app::meshchat
