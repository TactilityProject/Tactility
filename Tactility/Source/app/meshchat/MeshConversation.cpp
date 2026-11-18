
#define L10N_APP "mesh"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/meshchat/MeshChatCommon.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/std/deque.hpp>
#include <tactility/std/map.hpp>
#include <tactility/std/vector.hpp>

#include <mesh/mesh.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/toolbar.h>

#include <cstring>
#include <ctime>
#include <string>
#include <localisation/tr.h>

namespace tt::app::meshchat {

extern const ::AppManifest conversationManifest;

namespace {

constexpr auto* TAG = "MeshConversation";
constexpr size_t MESSAGE_LIMIT = 30;

/** The counts of a message that is retried or repeated. They are only known while this app listens. */
struct DeliveryDetail {
    uint8_t retryAttempt = 0;
    uint8_t retryMax = 0;
    uint8_t repeats = 0;
};

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
    /** By seq of the message. Changed with the LVGL lock held, as the widgets are rebuilt from it. */
    tt::std::map<uint32_t, DeliveryDetail> details;
    // Valid while the window is shown
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* input = nullptr;
};

const MeshAddress* getPeer(const Context* ctx) {
    return ctx->isContact ? &ctx->peer : nullptr;
}

/** How far a message got. Channel messages are never acknowledged by their recipients. */
::std::string getReceiptText(MeshDeliveryState delivery, const DeliveryDetail& detail = {}) {
    switch (delivery) {
        case MESH_DELIVERY_QUEUED:
            return tr("Sending...");
        case MESH_DELIVERY_BLOCKED:
            return tr("Waiting");
        case MESH_DELIVERY_SENT:
            return tr("Sent");
        case MESH_DELIVERY_RETRYING:
            return detail.retryMax == 0 ? ::std::string(tr("Retrying...")) : i18n::format(tr("Retrying {}/{}..."), { ::std::to_string(detail.retryAttempt), ::std::to_string(detail.retryMax) });
        case MESH_DELIVERY_REPEATED:
            if (detail.repeats == 0) {
                return tr("Heard repeats");
            }
            return detail.repeats == 1 ? ::std::string(tr("Heard 1 repeat")) : i18n::format(tr("Heard {} repeats"), { ::std::to_string(detail.repeats) });
        case MESH_DELIVERY_DELIVERED:
            return ::std::string(LV_SYMBOL_OK " ") + tr("Delivered");
        case MESH_DELIVERY_FAILED:
            return ::std::string(LV_SYMBOL_CLOSE " ") + tr("Not delivered");
        case MESH_DELIVERY_CANCELLED:
            return tr("Cancelled");
        default:
            return "";
    }
}

/** Requires the LVGL lock */
::std::string getStatusText(const Context* ctx, const MeshMessage& message) {
    const auto detail = ctx->details.find(message.seq);
    return getReceiptText(message.delivery, detail != ctx->details.end() ? detail->second : DeliveryDetail {});
}

::std::string getMetaText(const MeshMessage& message) {
    ::std::string meta;
    if (message.wall_time > 0) {
        const time_t time = (time_t)message.wall_time;
        struct tm local {};
        char text[8];
        if (localtime_r(&time, &local) != nullptr && strftime(text, sizeof(text), "%H:%M", &local) > 0) {
            meta = text;
        }
    }
    return meta;
}

/** Same name, same colour. The shade follows the theme so the theme's text stays readable on top of it. */
lv_color_t getSenderColor(lv_obj_t* obj, const MeshMessage& message) {
    if (message.outgoing) {
        return lv_theme_get_color_secondary(obj);
    }
    uint32_t hash = 2166136261u;
    for (const char* c = message.sender_name; *c != '\0'; c++) {
        hash = (hash ^ static_cast<uint8_t>(*c)) * 16777619u;
    }
    // Mixes the bits, as the hash of similar names differs little in its low bits. The saturation and value vary too, so
    // names that end up with a similar hue still get different colours.
    hash ^= hash >> 16;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35u;
    hash ^= hash >> 16;
    const auto color = lv_color_hsv_to_rgb(hash % 360, 55 + ((hash >> 9) % 3) * 20, 60 + ((hash >> 13) % 3) * 15);
    const bool darkTheme = lv_color_luminance(lv_obj_get_style_text_color(obj, LV_PART_MAIN)) > 127;
    return darkTheme ? lv_color_darken(color, LV_OPA_40) : lv_color_lighten(color, LV_OPA_50);
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

/** The theme's text colour, dimmed towards the background */
lv_color_t getMutedTextColor(lv_obj_t* obj) {
    const auto color = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    return lv_color_luminance(color) > 127 ? lv_color_darken(color, LV_OPA_40) : lv_color_lighten(color, LV_OPA_40);
}

/** The monochrome theme has no colours to tell senders apart, so the bar contrasts fully with the background */
lv_color_t getBarColor(lv_obj_t* obj, const MeshMessage& message, lv_color_t senderColor) {
    if (lvgl_theme_is_mono()) {
        return lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    }
    if (message.outgoing && message.delivery == MESH_DELIVERY_FAILED) {
        LvglThemeSettings theme {};
        lvgl_theme_get_settings(&theme);
        return theme.color_error;
    }
    return senderColor;
}

/** Requires the LVGL lock. Messages of the user are right-aligned. */
void createMessageItem(Context* ctx, const MeshMessage& message) {
    const bool right = message.outgoing;
    const auto color = getSenderColor(ctx->list, message);

    auto* item = lv_obj_create(ctx->list);
    lv_obj_set_size(item, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(item, LV_DPX(4), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(item, LV_DPX(2), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(item, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(item, false);
    lv_obj_set_user_data(item, reinterpret_cast<void*>(static_cast<uintptr_t>(message.seq)));
    const auto align = right ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;

    // The bar is the content's border on the side of the alignment
    auto* content = lv_obj_create(item);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(content, LV_DPX(3), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(content, getBarColor(ctx->list, message, color), LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(content, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(content, right ? LV_BORDER_SIDE_RIGHT : LV_BORDER_SIDE_LEFT, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(content, right ? 0 : LV_DPX(6), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(content, right ? LV_DPX(6) : 0, LV_STATE_DEFAULT);

    auto* header = lv_obj_create(content);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    // Reversed, the delivery state is left of the name
    lv_obj_set_flex_flow(header, right ? LV_FLEX_FLOW_ROW_REVERSE : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, right ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, LV_DPX(6), LV_STATE_DEFAULT);

    auto* nameLabel = lv_label_create(header);
    const ::std::string name = message.outgoing ? tr("You") : (message.sender_name[0] != '\0' ? message.sender_name : "?");
    lv_label_set_text(nameLabel, name.c_str());
    lv_obj_set_style_text_font(nameLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(nameLabel, color, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(nameLabel, LV_OPA_COVER, LV_STATE_DEFAULT);
    // Black or white, whichever contrasts most with the background
    lv_obj_set_style_text_color(nameLabel, lv_color_luminance(color) > 140 ? lv_color_black() : lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(nameLabel, LV_DPX(6), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(nameLabel, LV_DPX(6), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(nameLabel, LV_DPX(1), LV_STATE_DEFAULT);

    // Time
    auto* metaLabel = lv_label_create(header);
    lv_label_set_text(metaLabel, getMetaText(message).c_str());
    lv_obj_set_style_text_font(metaLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    auto* text = lv_label_create(content);
    lv_obj_set_width(text, LV_PCT(100));
    lv_obj_set_style_text_align(text, align, LV_STATE_DEFAULT);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(text, message.text);

    if (message.outgoing) {
        auto* status = lv_label_create(content);
        lv_obj_set_width(status, LV_PCT(100));
        lv_obj_set_style_text_align(status, align, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(status, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(status, getMutedTextColor(status), LV_STATE_DEFAULT);
        lv_label_set_text(status, getStatusText(ctx, message).c_str());
    }
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
    lv_label_set_text(ctx->statusLabel, i18n::format(tr("Mesh: {}"), { mesh_state_to_string(mesh_get_state()) }).c_str());
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
        lv_label_set_text(ctx->statusLabel, error == ERROR_INVALID_STATE ? tr("Turn mesh on to send messages") : error_to_string(error));
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
    lv_textarea_set_placeholder_text(ctx->input, tr("Type a message..."));
    lv_obj_add_event_cb(ctx->input, onSendPressed, LV_EVENT_READY, ctx);

    auto* sendButton = lv_button_create(inputBar);
    lv_obj_add_event_cb(sendButton, onSendPressed, LV_EVENT_CLICKED, ctx);
    lv_label_set_text(lv_label_create(sendButton), tr("Send"));
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
    const uint32_t trimmedSeq = trimmed ? ctx->messages.front().seq : 0;
    if (trimmed) {
        ctx->messages.pop_front();
    }

    lvgl_lock();
    ctx->details.erase(trimmedSeq);
    if (ctx->list != nullptr) {
        if (trimmed) {
            lv_obj_delete(lv_obj_get_child(ctx->list, 0));
        }
        createMessageItem(ctx, message);
        scrollToNewest(ctx);
    }
    lvgl_unlock();
}

void onDelivery(Context* ctx, uint32_t seq, MeshDeliveryState delivery, uint8_t retryAttempt, uint8_t retryMax, uint8_t repeats) {
    for (auto& message : ctx->messages) {
        if (message.seq != seq) {
            continue;
        }
        message.delivery = delivery;
        lvgl_lock();
        if (delivery == MESH_DELIVERY_RETRYING || delivery == MESH_DELIVERY_REPEATED) {
            ctx->details[seq] = { .retryAttempt = retryAttempt, .retryMax = retryMax, .repeats = repeats };
        } else {
            ctx->details.erase(seq);
        }
        if (ctx->list != nullptr) {
            const uint32_t count = lv_obj_get_child_count(ctx->list);
            for (uint32_t i = 0; i < count; i++) {
                auto* item = lv_obj_get_child(ctx->list, (int32_t)i);
                if (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(item))) == seq) {
                    auto* content = lv_obj_get_child(item, 0);
                    lv_obj_set_style_border_color(content, getBarColor(ctx->list, message, getSenderColor(ctx->list, message)), LV_STATE_DEFAULT);
                    auto* header = lv_obj_get_child(content, 0);
                    lv_label_set_text(lv_obj_get_child(header, 1), getMetaText(message).c_str());
                    if (message.outgoing) {
                        lv_label_set_text(lv_obj_get_child(content, 2), getStatusText(ctx, message).c_str());
                    }
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
        ctx->messageDialogId = alertdialog::start(ctx->appInstanceId, tr("Channel key"), tr("The key isn't available."));
        return;
    }
    MeshAddress key {};
    memcpy(key.bytes, channel.key, channel.key_length);
    key.length = channel.key_length;
    const ::std::string text = addressToHex(key);
    ctx->messageDialogId = alertdialog::start(ctx->appInstanceId, tr("Channel key"), text.c_str());
}

void startRemoveDialog(Context* ctx) {
    if (ctx->removeDialogId != 0) {
        return;
    }
    const char* title = ctx->isContact ? tr("Remove contact") : tr("Leave channel");
    const char* message = ctx->isContact ? tr("The messages are kept.") : tr("The messages are kept, but new messages in this channel aren't received anymore.");
    ctx->removeDialogId = alertdialog::start(ctx->appInstanceId, title, message, { tr("Remove"), tr("Cancel") });
}

/** @return true when the conversation is gone and the app can close */
bool onRemoveResult(Context* ctx, int32_t result) {
    if (result != 0) {
        return false;
    }
    const error_t error = ctx->isContact ? mesh_remove_contact(&ctx->peer) : mesh_remove_channel(ctx->channel);
    if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
        LOG_E(TAG, "Failed to remove: %s", error_to_string(error));
        const char* reason = (error == ERROR_INVALID_ARGUMENT) ? tr("This channel can't be left.") : error_to_string(error);
        ctx->messageDialogId = alertdialog::start(ctx->appInstanceId, tr("Couldn't remove"), reason);
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
                    onDelivery(&ctx, meshEvent.seq, meshEvent.delivery, meshEvent.retry_attempt, meshEvent.retry_max, meshEvent.repeat_count);
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

