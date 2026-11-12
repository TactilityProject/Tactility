
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/meshchat/MeshChatCommon.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/std/vector.hpp>

#include <mesh/mesh.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <atomic>
#include <cstring>
#include <string>

namespace tt::app::meshchat {

extern const ::AppManifest addContactManifest;

namespace {

constexpr auto* TAG = "MeshAddContact";
constexpr size_t CONTACT_LIMIT = 64;
constexpr size_t NODE_LIMIT = 128;

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t addBit = 0;
    uint32_t detailsBit = 0;
    uint32_t dialogId = 0;
    /** Written by the LVGL thread before it signals addBit or detailsBit */
    ::std::atomic<int32_t> selectedIndex = 0;
    /** The nodes that aren't contacts yet */
    tt::std::vector<MeshNode> candidates;
    // Valid while the window is shown
    lv_obj_t* list = nullptr;
};

::std::string getNodeName(const MeshNode& node) {
    return node.name[0] != '\0' ? ::std::string(node.name) : addressToHex(node.address);
}

/** Queries the service thread, so it's called without the LVGL lock */
void loadCandidates(Context* ctx) {
    tt::std::vector<MeshContact> contacts(CONTACT_LIMIT);
    size_t contactCount = 0;
    if (mesh_get_contacts(contacts.data(), CONTACT_LIMIT, &contactCount) != ERROR_NONE) {
        contactCount = 0;
    }

    ctx->candidates.assign(NODE_LIMIT, MeshNode {});
    size_t count = 0;
    if (mesh_get_nodes(ctx->candidates.data(), NODE_LIMIT, &count) != ERROR_NONE) {
        count = 0;
    }
    ctx->candidates.resize(count);
    for (size_t i = ctx->candidates.size(); i > 0; i--) {
        const auto& node = ctx->candidates[i - 1];
        for (size_t c = 0; c < contactCount; c++) {
            if (contacts[c].address.length == node.address.length && memcmp(contacts[c].address.bytes, node.address.bytes, node.address.length) == 0) {
                ctx->candidates.erase(ctx->candidates.begin() + (i - 1));
                break;
            }
        }
    }
}

void onSelected(lv_event_t* event, uint32_t Context::*bit) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->selectedIndex = (int32_t)reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    task_event_group_signal(ctx->eventGroup, ctx->*bit);
}

// Short click, so that the end of a long press doesn't add the contact
void onNodeClicked(lv_event_t* event) {
    onSelected(event, &Context::addBit);
}

void onNodeLongPressed(lv_event_t* event) {
    onSelected(event, &Context::detailsBit);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

/** Requires the LVGL lock */
void updateWidgets(Context* ctx) {
    if (ctx->list == nullptr) {
        return;
    }
    lv_obj_clean(ctx->list);

    auto* card = lvgl_card_create(ctx->list);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 0, LV_STATE_DEFAULT);
    // The list items' pressed and focused backgrounds follow the card's rounded corners
    lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);

    // The card provides the background
    auto* list = lvgl_list_create(card);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);

    if (ctx->candidates.empty()) {
        lvgl_list_add_text(list, "No nodes heard yet. Nodes that you hear on the mesh are listed here.");
        return;
    }
    for (size_t i = 0; i < ctx->candidates.size(); i++) {
        const auto& node = ctx->candidates[i];
        auto* button = lvgl_list_add_button(list, nullptr, getNodeName(node).c_str());
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(button, onNodeClicked, LV_EVENT_SHORT_CLICKED, ctx);
        lv_obj_add_event_cb(button, onNodeLongPressed, LV_EVENT_LONG_PRESSED, ctx);

        auto* name = lv_obj_get_child(button, lv_obj_get_child_count(button) - 1);
        // The button stacks the name and the hash, so the name doesn't grow along the row
        lv_obj_set_flex_grow(name, 0);
        lv_obj_set_width(name, LV_PCT(100));
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

        // The remaining data is cut off with an ellipsis, and shown completely on a long press
        auto* hash = lv_label_create(button);
        lv_obj_set_width(hash, LV_PCT(100));
        lv_obj_set_style_text_font(hash, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
        lv_label_set_long_mode(hash, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(hash, addressToHex(node.address).c_str());
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Add contact");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    ctx->list = lv_obj_create(parent);
    lv_obj_set_width(ctx->list, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->list, 1);
    lv_obj_set_flex_flow(ctx->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(ctx->list, LV_DPX(4), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(ctx->list, LV_DPX(4), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->list, 0, LV_STATE_DEFAULT);

    updateWidgets(ctx);
}

void destroyWidgets(void* userData) {
    static_cast<Context*>(userData)->list = nullptr;
}

/** @return true when the contact was added */
bool addSelected(Context* ctx) {
    const auto index = (size_t)ctx->selectedIndex.load();
    if (index >= ctx->candidates.size()) {
        return false;
    }
    const auto& node = ctx->candidates[index];
    const error_t error = mesh_add_contact(&node.address, node.name);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add contact: %s", error_to_string(error));
        ctx->dialogId = alertdialog::start(
            ctx->appInstanceId,
            "Couldn't add contact",
            error == ERROR_RESOURCE ? "There are too many contacts." : error_to_string(error)
        );
        return false;
    }
    return true;
}

void showDetails(Context* ctx) {
    const auto index = (size_t)ctx->selectedIndex.load();
    if (ctx->dialogId == 0 && index < ctx->candidates.size()) {
        const auto& node = ctx->candidates[index];
        ctx->dialogId = alertdialog::start(ctx->appInstanceId, getNodeName(node), addressToHex(node.address));
    }
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    ctx.eventGroup = &eventGroup;
    check(task_event_group_claim_bit(&eventGroup, &ctx.addBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.detailsBit) == ERROR_NONE);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);
    MeshEventSubscription meshEvents {};
    check(mesh_event_subscribe(&meshEvents, &eventGroup) == ERROR_NONE);

    loadCandidates(&ctx);
    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, portMAX_DELAY);

        if (flags & ctx.addBit) {
            shouldClose |= addSelected(&ctx);
        }
        if (flags & ctx.detailsBit) {
            showDetails(&ctx);
        }

        // Nodes that are heard while the list is empty show up without reopening it
        bool changed = false;
        MeshEvent meshEvent {};
        while (mesh_event_poll(&meshEvents, &meshEvent) == ERROR_NONE) {
            changed |= meshEvent.type == MESH_EVENT_NODE && ctx.candidates.empty();
        }

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
            if (event.type == APP_EVENT_RESULT) {
                if (event.result.launch_id == ctx.dialogId) {
                    ctx.dialogId = 0;
                }
                app_manager_stop(event.result.launch_id);
            }
        }

        if (changed && !shouldClose) {
            loadCandidates(&ctx);
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

extern const ::AppManifest addContactManifest = {
    .id = ADD_CONTACT_APP_ID,
    .name = "Add Contact",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace tt::app::meshchat

