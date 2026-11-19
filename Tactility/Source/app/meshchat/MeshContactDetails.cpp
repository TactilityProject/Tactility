#define L10N_APP "mesh"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/meshchat/MeshChatCommon.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <mesh/mesh.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/time.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <cmath>
#include <string>

namespace tt::app::meshchat {

extern const ::AppManifest contactDetailsManifest;

namespace {

constexpr size_t CONTACT_LIMIT = 64;
constexpr size_t NODE_LIMIT = 128;

struct Context {
    uint32_t appInstanceId;
    MeshAddress address {};
    /** One line per fact. Filled by the app's task, shown by updateWidgets(). */
    tt::std::vector<tt::std::string> lines;
    // Valid while the window is shown
    lv_obj_t* content = nullptr;
};

::std::string formatAge(uint64_t thenMs) {
    using i18n::format;
    const uint64_t nowMs = get_micros_since_boot() / 1000;
    const uint64_t seconds = nowMs > thenMs ? (nowMs - thenMs) / 1000 : 0;
    if (seconds < 60) {
        return format(tr("{} s ago"), { ::std::to_string(seconds) });
    }
    if (seconds < 3600) {
        return format(tr("{} min ago"), { ::std::to_string(seconds / 60) });
    }
    return format(tr("{} h ago"), { ::std::to_string(seconds / 3600) });
}

/** The hashes like "a1,b2,c3" */
::std::string formatPath(const MeshContactPath& path) {
    ::std::string text;
    char hex[3];
    for (size_t hop = 0; hop < path.hop_count; hop++) {
        if (hop > 0) {
            text += ",";
        }
        for (size_t i = 0; i < path.hash_size; i++) {
            snprintf(hex, sizeof(hex), "%02x", path.hops[hop * path.hash_size + i]);
            text += hex;
        }
    }
    return text;
}

/** Queries the service thread, so it's called without the LVGL lock */
void buildLines(Context* ctx, tt::std::vector<tt::std::string>& lines) {
    using i18n::format;
    auto add = [&lines](const ::std::string& line) { lines.push_back(line.c_str()); };

    tt::std::vector<MeshContact> contacts(CONTACT_LIMIT);
    size_t contactCount = 0;
    if (mesh_get_contacts(contacts.data(), CONTACT_LIMIT, &contactCount) != ERROR_NONE) {
        add(getNotReadyText());
        return;
    }
    const MeshContact* contact = nullptr;
    for (size_t i = 0; i < contactCount; i++) {
        if (contacts[i].address.length == ctx->address.length && memcmp(contacts[i].address.bytes, ctx->address.bytes, ctx->address.length) == 0) {
            contact = &contacts[i];
        }
    }
    add(format(tr("Name: {}"), { contact != nullptr ? contact->name : "?" }));
    add(format(tr("Public key: {}"), { addressToHex(ctx->address) }));

    MeshContactPath path {};
    const error_t pathResult = mesh_get_contact_path(&ctx->address, &path);
    if (pathResult == ERROR_NONE) {
        add(format(path.manual ? tr("Path (set by you): {}") : tr("Path (learned): {}"), { formatPath(path) }));
    } else if (pathResult == ERROR_NOT_FOUND) {
        add(tr("Path: none, messages are flooded"));
    }

    tt::std::vector<MeshNode> nodes(NODE_LIMIT);
    size_t nodeCount = 0;
    if (mesh_get_nodes(nodes.data(), NODE_LIMIT, &nodeCount) != ERROR_NONE) {
        nodeCount = 0;
    }
    const MeshNode* node = nullptr;
    for (size_t i = 0; i < nodeCount; i++) {
        if (nodes[i].address.length == ctx->address.length && memcmp(nodes[i].address.bytes, ctx->address.bytes, ctx->address.length) == 0) {
            node = &nodes[i];
        }
    }
    if (node != nullptr && node->last_advert_ms != 0) {
        add(format(tr("Last advert heard: {}"), { formatAge(node->last_advert_ms) }));
    } else {
        add(tr("Last advert heard: not since start"));
    }
    if (node != nullptr) {
        add(format(tr("Last heard: {}"), { formatAge(node->last_heard_ms) }));
        const ::std::string rssi = ::std::to_string(static_cast<int>(::std::lround(node->rssi)));
        if (::std::isnan(node->snr)) {
            add(format(tr("Signal: {} dBm"), { rssi }));
        } else {
            add(format(tr("Signal: {} dBm, {} dB"), { rssi, ::std::to_string(static_cast<int>(::std::lround(node->snr))) }));
        }
    }
}

/** The lines are built first, as the window can be rebuilt on another thread while this one reads the service */
void loadLines(Context* ctx) {
    tt::std::vector<tt::std::string> lines;
    buildLines(ctx, lines);
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
    lv_obj_update_layout(ctx->content);
    lv_obj_scroll_to_y(ctx->content, scrollY, LV_ANIM_OFF);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Details"));
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
    if (argc < 1 || !hexToAddress(argv[0], &ctx.address)) {
        return 1;
    }

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

        bool changed = false;
        MeshEvent meshEvent {};
        while (mesh_event_poll(&meshEvents, &meshEvent) == ERROR_NONE) {
            changed |= meshEvent.type == MESH_EVENT_NODE || meshEvent.type == MESH_EVENT_CONTACT || meshEvent.type == MESH_EVENT_STATE;
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

extern const ::AppManifest contactDetailsManifest = {
    .id = CONTACT_DETAILS_APP_ID,
    .name = "Mesh Contact Details",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace tt::app::meshchat
