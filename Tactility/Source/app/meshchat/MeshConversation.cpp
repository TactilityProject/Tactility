
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/meshchat/MeshChatCommon.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/std/deque.hpp>
#include <tactility/std/vector.hpp>

#include <mesh/mesh.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <cstring>
#include <ctime>
#include <string>

namespace tt::app::meshchat {

extern const ::AppManifest conversationManifest;

namespace {

constexpr auto* TAG = "MeshConversation";
constexpr size_t MESSAGE_LIMIT = 30;

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t removeBit = 0;
    uint32_t infoBit = 0;
    uint32_t removeDialogId = 0;
    uint32_t messageDialogId = 0;
    bool isContact = true;
    /** A channel with a key that isn't derived from its name, so it has to be shared */
    bool isPrivateChannel = false;
    MeshAddress peer {};
    uint8_t channel = 0;
    ::std::string title;
    /** Oldest first */
    tt::std::deque<MeshMessage> messages;
    // Valid while the window is shown
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* input = nullptr;
};

const MeshAddress* getPeer(const Context* ctx) {
    return ctx->isContact ? &ctx->peer : nullptr;
}

/** How far a message got. Channel messages are never acknowledged by their recipients. */
const char* getReceiptText(MeshDeliveryState delivery) {
    switch (delivery) {
        case MESH_DELIVERY_QUEUED:
            return "Sending...";
        case MESH_DELIVERY_BLOCKED:
            return "Waiting";
        case MESH_DELIVERY_SENT:
            return "Sent";
        case MESH_DELIVERY_DELIVERED:
            return LV_SYMBOL_OK " Delivered";
        case MESH_DELIVERY_FAILED:
            return LV_SYMBOL_CLOSE " Not delivered";
        case MESH_DELIVERY_CANCELLED:
            return "Cancelled";
        default:
            return "";
    }
}

::std::string getHeaderText(const MeshMessage& message) {
    ::std::string header = message.outgoing ? "You" : (message.sender_name[0] != '\0' ? message.sender_name : "?");
    if (message.wall_time > 0) {
        const time_t time = (time_t)message.wall_time;
        struct tm local {};
        char text[8];
        if (localtime_r(&time, &local) != nullptr && strftime(text, sizeof(text), "%H:%M", &local) > 0) {
            header += "  ";
            header += text;
        }
    }
    if (message.outgoing) {
        header += "  ";
        header += getReceiptText(message.delivery);
    }
    return header;
}

bool belongsToConversation(const Context* ctx, const MeshMessage& message) {
    char backend[MESH_BACKEND_ID_MAX_LENGTH + 1];
    if (mesh_get_backend(backend, sizeof(backend)) != ERROR_NONE || strncmp(message.backend, backend, MESH_BACKEND_ID_MAX_LENGTH) != 0) {
        return false;
    }
    if (ctx->isContact) {
        return message.peer.length == ctx->peer.length && memcmp(message.peer.bytes, ctx->peer.bytes, ctx->peer.length) == 0;
    }
    return message.peer.length == 0 && message.channel == ctx->channel;
}

/** Requires the LVGL lock */
void createMessageItem(Context* ctx, const MeshMessage& message) {
    auto* item = lv_obj_create(ctx->list);
    lv_obj_set_size(item, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(item, LV_DPX(4), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(item, LV_DPX(2), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(item, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(item, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(item, false);
    lv_obj_set_user_data(item, reinterpret_cast<void*>(static_cast<uintptr_t>(message.seq)));
    const auto align = message.outgoing ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;

    auto* header = lv_label_create(item);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_style_text_align(header, align, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(header, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_label_set_text(header, getHeaderText(message).c_str());

    auto* text = lv_label_create(item);
    lv_obj_set_width(text, LV_PCT(100));
    lv_obj_set_style_text_align(text, align, LV_STATE_DEFAULT);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(text, message.text);
}

/** Requires the LVGL lock */
void scrollToNewest(Context* ctx) {
    lv_obj_update_layout(ctx->list);
    lv_obj_scroll_to_y(ctx->list, LV_COORD_MAX, LV_ANIM_OFF);
}

/** Requires the LVGL lock */
void updateStatus(Context* ctx) {
    if (ctx->statusLabel == nullptr) {
        return;
    }
    const bool on = mesh_get_state() == MESH_STATE_ON;
    lv_label_set_text(ctx->statusLabel, (::std::string("Mesh: ") + mesh_state_to_string(mesh_get_state())).c_str());
    lv_obj_set_hidden(ctx->statusLabel, on);
}

void onSendPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const char* text = lv_textarea_get_text(ctx->input);
    if (text[0] == '\0') {
        return;
    }
    const error_t error = mesh_send_text(getPeer(ctx), ctx->channel, text, nullptr);
    if (error == ERROR_NONE) {
        lv_textarea_set_text(ctx->input, "");
    } else {
        LOG_W(TAG, "Failed to send: %s", error_to_string(error));
        lv_label_set_text(ctx->statusLabel, error == ERROR_INVALID_STATE ? "Turn mesh on to send messages" : error_to_string(error));
        lv_obj_set_hidden(ctx->statusLabel, false);
    }
}

void onRemovePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->removeBit);
}

void onInfoPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->infoBit);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

/** Adds a toolbar action that shows a shared icon, which the text font doesn't have */
void addIconAction(lv_obj_t* toolbar, const char* icon, lv_event_cb_t callback, void* userData) {
    auto* button = lvgl_toolbar_add_text_button_action(toolbar, icon, callback, userData);
    lv_obj_set_style_text_font(button, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, ctx->title.c_str());
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    if (ctx->isPrivateChannel) {
        addIconAction(toolbar, LVGL_ICON_SHARED_INFO, onInfoPressed, ctx);
    }
    addIconAction(toolbar, LVGL_ICON_SHARED_PERSON_REMOVE, onRemovePressed, ctx);

    ctx->statusLabel = lv_label_create(parent);
    lv_obj_set_width(ctx->statusLabel, LV_PCT(100));
    lv_obj_set_style_pad_hor(ctx->statusLabel, LV_DPX(8), LV_STATE_DEFAULT);
    updateStatus(ctx);

    ctx->list = lv_obj_create(parent);
    lv_obj_set_width(ctx->list, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->list, 1);
    lv_obj_set_flex_flow(ctx->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->list, LV_DPX(2), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->list, 0, LV_STATE_DEFAULT);
    // Lets the keys scroll the history
    if (auto* group = lv_group_get_default()) {
        lv_group_add_obj(group, ctx->list);
    }
    for (const auto& message : ctx->messages) {
        createMessageItem(ctx, message);
    }
    scrollToNewest(ctx);

    auto* inputBar = lv_obj_create(parent);
    lv_obj_set_size(inputBar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(inputBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(inputBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(inputBar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(inputBar, LV_DPX(4), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(inputBar, LV_DPX(4), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(inputBar, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(inputBar, false);

    ctx->input = lv_textarea_create(inputBar);
    lv_obj_set_flex_grow(ctx->input, 1);
    lv_textarea_set_one_line(ctx->input, true);
    lv_textarea_set_max_length(ctx->input, MESH_TEXT_MAX_LENGTH);
    lv_textarea_set_placeholder_text(ctx->input, "Type a message...");
    lv_obj_add_event_cb(ctx->input, onSendPressed, LV_EVENT_READY, ctx);

    auto* sendButton = lv_button_create(inputBar);
    lv_obj_add_event_cb(sendButton, onSendPressed, LV_EVENT_CLICKED, ctx);
    lv_label_set_text(lv_label_create(sendButton), "Send");
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->statusLabel = nullptr;
    ctx->list = nullptr;
    ctx->input = nullptr;
}

/** Queries the service, so it's called without the LVGL lock */
void loadMessages(Context* ctx) {
    tt::std::vector<MeshMessage> history(MESSAGE_LIMIT);
    size_t count = 0;
    if (mesh_conversation_get(getPeer(ctx), ctx->channel, 0, history.data(), MESSAGE_LIMIT, &count) != ERROR_NONE) {
        return;
    }
    // History is newest first
    for (size_t i = count; i > 0; i--) {
        ctx->messages.push_back(history[i - 1]);
    }
}

void onMessage(Context* ctx, uint32_t seq) {
    for (const auto& known : ctx->messages) {
        if (known.seq == seq) {
            return;
        }
    }
    MeshMessage message;
    if (mesh_message_get(seq, &message) != ERROR_NONE || !belongsToConversation(ctx, message)) {
        return;
    }
    ctx->messages.push_back(message);
    const bool trimmed = ctx->messages.size() > MESSAGE_LIMIT;
    if (trimmed) {
        ctx->messages.pop_front();
    }

    lvgl_lock();
    if (ctx->list != nullptr) {
        if (trimmed) {
            lv_obj_delete(lv_obj_get_child(ctx->list, 0));
        }
        createMessageItem(ctx, message);
        scrollToNewest(ctx);
    }
    lvgl_unlock();
}

void onDelivery(Context* ctx, uint32_t seq, MeshDeliveryState delivery) {
    for (auto& message : ctx->messages) {
        if (message.seq != seq) {
            continue;
        }
        message.delivery = delivery;
        lvgl_lock();
        if (ctx->list != nullptr) {
            const uint32_t count = lv_obj_get_child_count(ctx->list);
            for (uint32_t i = 0; i < count; i++) {
                auto* item = lv_obj_get_child(ctx->list, (int32_t)i);
                if (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(item))) == seq) {
                    lv_label_set_text(lv_obj_get_child(item, 0), getHeaderText(message).c_str());
                    break;
                }
            }
        }
        lvgl_unlock();
        return;
    }
}

/** Looks up the channel. @return false when it isn't known */
bool getChannel(const Context* ctx, MeshChannel* channel) {
    MeshChannel channels[8];
    size_t count = 0;
    if (mesh_get_channels(channels, 8, &count) != ERROR_NONE) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (channels[i].index == ctx->channel) {
            *channel = channels[i];
            return true;
        }
    }
    return false;
}

void showChannelKey(Context* ctx) {
    if (ctx->messageDialogId != 0) {
        return;
    }
    MeshChannel channel;
    if (!getChannel(ctx, &channel) || channel.key_length == 0) {
        ctx->messageDialogId = alertdialog::start(ctx->appInstanceId, "Channel key", "The key isn't available.");
        return;
    }
    MeshAddress key {};
    memcpy(key.bytes, channel.key, channel.key_length);
    key.length = channel.key_length;
    const ::std::string text = addressToHex(key);
    ctx->messageDialogId = alertdialog::start(ctx->appInstanceId, "Channel key", text.c_str());
}

void startRemoveDialog(Context* ctx) {
    if (ctx->removeDialogId != 0) {
        return;
    }
    const char* title = ctx->isContact ? "Remove contact" : "Leave channel";
    const char* message = ctx->isContact ? "The messages are kept." : "The messages are kept, but new messages in this channel aren't received anymore.";
    ctx->removeDialogId = alertdialog::start(ctx->appInstanceId, title, message, { "Remove", "Cancel" });
}

/** @return true when the conversation is gone and the app can close */
bool onRemoveResult(Context* ctx, int32_t result) {
    if (result != 0) {
        return false;
    }
    const error_t error = ctx->isContact ? mesh_remove_contact(&ctx->peer) : mesh_remove_channel(ctx->channel);
    if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
        LOG_E(TAG, "Failed to remove: %s", error_to_string(error));
        const char* reason = (error == ERROR_INVALID_ARGUMENT) ? "This channel can't be left." : error_to_string(error);
        ctx->messageDialogId = alertdialog::start(ctx->appInstanceId, "Couldn't remove", reason);
        return false;
    }
    return true;
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();
    if (argc < 3) {
        return 1;
    }
    ctx.isContact = strcmp(argv[0], KIND_CONTACT) == 0;
    if (ctx.isContact) {
        if (!hexToAddress(argv[1], &ctx.peer)) {
            return 1;
        }
    } else {
        ctx.channel = (uint8_t)atoi(argv[1]);
        MeshChannel channel;
        ctx.isPrivateChannel = ctx.channel != 0 && getChannel(&ctx, &channel) && channel.name[0] != '#';
    }
    ctx.title = argv[2];

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    ctx.eventGroup = &eventGroup;
    check(task_event_group_claim_bit(&eventGroup, &ctx.removeBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.infoBit) == ERROR_NONE);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);
    MeshEventSubscription meshEvents {};
    check(mesh_event_subscribe(&meshEvents, &eventGroup) == ERROR_NONE);

    // The window shows up first, and the history follows
    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);
    loadMessages(&ctx);
    lvgl_lock();
    if (ctx.list != nullptr) {
        // The window may have been rebuilt from the messages meanwhile
        lv_obj_clean(ctx.list);
        for (const auto& message : ctx.messages) {
            createMessageItem(&ctx, message);
        }
        scrollToNewest(&ctx);
    }
    lvgl_unlock();

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, portMAX_DELAY);

        if (flags & ctx.removeBit) {
            startRemoveDialog(&ctx);
        }
        if (flags & ctx.infoBit) {
            showChannelKey(&ctx);
        }

        MeshEvent meshEvent {};
        while (mesh_event_poll(&meshEvents, &meshEvent) == ERROR_NONE) {
            switch (meshEvent.type) {
                case MESH_EVENT_MESSAGE:
                    onMessage(&ctx, meshEvent.seq);
                    break;
                case MESH_EVENT_DELIVERY:
                    onDelivery(&ctx, meshEvent.seq, meshEvent.delivery);
                    break;
                case MESH_EVENT_STATE:
                    lvgl_lock();
                    updateStatus(&ctx);
                    lvgl_unlock();
                    break;
                default:
                    break;
            }
        }

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
            if (event.type == APP_EVENT_RESULT) {
                if (event.result.launch_id == ctx.removeDialogId) {
                    ctx.removeDialogId = 0;
                    shouldClose |= onRemoveResult(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.messageDialogId) {
                    ctx.messageDialogId = 0;
                }
                app_manager_stop(event.result.launch_id);
            }
        }
    }

    window_manager_remove(window);
    check(mesh_event_unsubscribe(&meshEvents) == ERROR_NONE);
    check(app_event_unsubscribe(&appEvents) == ERROR_NONE);
    task_event_group_destruct(&eventGroup);
    return 0;
}

} // namespace

extern const ::AppManifest conversationManifest = {
    .id = CONVERSATION_APP_ID,
    .name = "Mesh Conversation",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 6144, .desired_memory_capability = 0 },
};

} // namespace tt::app::meshchat

