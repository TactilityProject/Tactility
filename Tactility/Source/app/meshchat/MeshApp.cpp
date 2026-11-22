#define L10N_APP "mesh"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/inputdialog/InputDialog.h>
#include <Tactility/app/meshchat/MeshChatCommon.h>
#include <Tactility/app/meshsettings/MeshChannels.h>
#include <Tactility/app/qrcode/QrCode.h>
#include <Tactility/app/selectiondialog/SelectionDialog.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/Memory.hpp>
#include <tactility/std/deque.hpp>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <mesh/mesh.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/tabs.h>
#include <lvgl/widgets/toolbar.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace tt::app::meshchat {

extern const ::AppManifest manifest;

namespace {

constexpr auto* TAG = "Mesh";
constexpr size_t CHANNEL_LIMIT = 8;
constexpr size_t CONTACT_LIMIT = 64;
constexpr size_t INSPECT_LINE_LIMIT = 100;

/** Channels and Contacts are the tabs, in this order */
enum class ViewMode { Channels, Contacts, Inspect };

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t openBit = 0;
    uint32_t menuBit = 0;
    uint32_t settingsBit = 0;
    uint32_t addBit = 0;
    uint32_t showChannelsBit = 0;
    uint32_t showContactsBit = 0;
    ViewMode mode = ViewMode::Channels;
    /** The tab that Inspect returns to */
    ViewMode lastTab = ViewMode::Channels;
    /** Written by the LVGL thread before it signals openBit */
    ::std::atomic<int32_t> openIndex = 0;
    uint32_t menuDialogId = 0;
    uint32_t addTypeDialogId = 0;
    uint32_t addContactDialogId = 0;
    /** The kind of channel for each option of the open selection dialog */
    ::std::vector<int> addTypes;
    /** Whether the mesh network supports any way to add a channel. Read by the app's task. */
    bool canAddChannel = false;
    uint32_t joinChannelDialogId = 0;
    uint32_t createChannelDialogId = 0;
    uint32_t privateNameDialogId = 0;
    uint32_t privateKeyDialogId = 0;
    uint32_t contactNameDialogId = 0;
    uint32_t contactKeyDialogId = 0;
    uint32_t messageDialogId = 0;
    uint32_t pathContactDialogId = 0;
    uint32_t pathInputDialogId = 0;
    AppStream pathStream {};
    char pathBuffer[192] = {};
    /** Whether the open path flow resets the path instead of setting it */
    bool pathReset = false;
    /** The contact that the path flow is about */
    MeshAddress pathContact {};
    /** The contacts that the dialog showed, as the list of contacts can change while it is open */
    tt::std::vector<MeshAddress> pathContactChoices;
    /** The action of each option of the open menu */
    tt::std::vector<int> menuItems;
    AppStream channelStream {};
    char channelBuffer[MESH_NAME_MAX_LENGTH + 1] = {};
    AppStream createStream {};
    char createBuffer[MESH_NAME_MAX_LENGTH + 1] = {};
    AppStream privateNameStream {};
    char privateNameBuffer[MESH_NAME_MAX_LENGTH + 1] = {};
    AppStream privateKeyStream {};
    char privateKeyBuffer[MESH_CHANNEL_KEY_MAX_LENGTH * 2 + 1] = {};
    /** The name that was entered while the key is asked */
    ::std::string pendingChannelName;
    AppStream contactNameStream {};
    char contactNameBuffer[MESH_NAME_MAX_LENGTH + 1] = {};
    AppStream contactKeyStream {};
    char contactKeyBuffer[MESH_ADDRESS_MAX_LENGTH * 2 + 1] = {};
    /** The name that was entered while the key is asked */
    ::std::string pendingContactName;
    tt::std::vector<MeshChannel> channels;
    tt::std::vector<MeshContact> contacts;
    /** The unread messages of each channel and contact, in the order of the lists */
    tt::std::vector<uint32_t> channelUnread;
    tt::std::vector<uint32_t> contactUnread;
    /** The events since the app started, oldest first. Changed with the LVGL lock held. */
    tt::std::deque<tt::std::string> inspectLines;
    // Valid while the window is shown
    lv_obj_t* root = nullptr;
    lv_obj_t* networkHeader = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* addButton = nullptr;
    /** The tab bar, which shows the unread messages of the channels and contacts */
    lv_obj_t* tabs = nullptr;
    NetworkInfo network;
};

/** What loadData() fetched, committed to the context by commitData() */
struct Snapshot {
    NetworkInfo network;
    bool canAddChannel = false;
    tt::std::vector<MeshChannel> channels;
    tt::std::vector<MeshContact> contacts;
    tt::std::vector<uint32_t> channelUnread;
    tt::std::vector<uint32_t> contactUnread;
};

/** Queries the service thread, so it's called without the LVGL lock */
Snapshot loadData() {
    Snapshot snapshot;
    snapshot.network = loadNetworkInfo();
    snapshot.canAddChannel = mesh_supports(MESH_FEATURE_NAMED_CHANNELS) || mesh_supports(MESH_FEATURE_KEYED_CHANNELS);
    snapshot.channels.assign(CHANNEL_LIMIT, MeshChannel {});
    size_t count = 0;
    if (mesh_get_channels(snapshot.channels.data(), CHANNEL_LIMIT, &count) != ERROR_NONE) {
        count = 0;
    }
    snapshot.channels.resize(count);
    snapshot.contacts.assign(CONTACT_LIMIT, MeshContact {});
    count = 0;
    if (mesh_get_contacts(snapshot.contacts.data(), CONTACT_LIMIT, &count) != ERROR_NONE) {
        count = 0;
    }
    snapshot.contacts.resize(count);
    for (const auto& channel : snapshot.channels) {
        snapshot.channelUnread.push_back(mesh_get_unread(nullptr, channel.index));
    }
    for (const auto& contact : snapshot.contacts) {
        snapshot.contactUnread.push_back(mesh_get_unread(&contact.address, 0));
    }
    return snapshot;
}

/** Requires the LVGL lock, because create_widgets can run on another thread and reads the cached state */
void commitData(Context* ctx, Snapshot&& snapshot) {
    ctx->network = snapshot.network;
    ctx->canAddChannel = snapshot.canAddChannel;
    ctx->channels = ::std::move(snapshot.channels);
    ctx->contacts = ::std::move(snapshot.contacts);
    ctx->channelUnread = ::std::move(snapshot.channelUnread);
    ctx->contactUnread = ::std::move(snapshot.contactUnread);
}

uint32_t sumUnread(const tt::std::vector<uint32_t>& counts) {
    uint32_t total = 0;
    for (const uint32_t count : counts) {
        total += count;
    }
    // The counts stop at the limit, so a sum above it is shown as 99+ as well
    return total < MESH_UNREAD_COUNT_LIMIT ? total : MESH_UNREAD_COUNT_LIMIT;
}

/** Shows the number of unread messages and an envelope at the right of a list item, when there are any */
void addUnreadIndicator(lv_obj_t* button, uint32_t unread) {
    if (unread == 0) {
        return;
    }
    auto* count = lv_label_create(button);
    lv_label_set_text(count, unread >= MESH_UNREAD_COUNT_LIMIT ? "99+" : ::std::to_string(unread).c_str());
    auto* icon = lv_label_create(button);
    lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(icon, LVGL_ICON_SHARED_MAIL);
}

lv_obj_t* addListButton(lv_obj_t* list, const char* text, lv_event_cb_t callback, void* callbackData, intptr_t index, uint32_t unread = 0) {
    auto* button = lvgl_list_add_button(list, nullptr, text);
    addUnreadIndicator(button, unread);
    lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, callbackData);
    return button;
}

void onItemPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->openIndex = (int32_t)reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    task_event_group_signal(ctx->eventGroup, ctx->openBit);
}

void signalBit(lv_event_t* event, uint32_t Context::* bit) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->*bit);
}

void onAddPressed(lv_event_t* event) { signalBit(event, &Context::addBit); }
void onMenuPressed(lv_event_t* event) { signalBit(event, &Context::menuBit); }
void onSettingsPressed(lv_event_t* event) { signalBit(event, &Context::settingsBit); }

/** Switching tabs rebuilds the widgets, which can't happen in an event of the tab bar, so the app's task does it */
void onTabChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const bool contacts = lvgl_tabs_get_selected(lv_event_get_current_target_obj(event)) == static_cast<uint32_t>(ViewMode::Contacts);
    task_event_group_signal(ctx->eventGroup, contacts ? ctx->showContactsBit : ctx->showChannelsBit);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

/** Requires the LVGL lock */
lv_obj_t* addInspectLabel(Context* ctx, const tt::std::string& line) {
    auto* label = lv_label_create(ctx->content);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(label, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_label_set_text(label, line.c_str());
    return label;
}

/** Requires the LVGL lock */
void scrollInspectToNewest(Context* ctx) {
    lv_obj_update_layout(ctx->content);
    lv_obj_scroll_to_y(ctx->content, LV_COORD_MAX, LV_ANIM_OFF);
}

/** Requires the LVGL lock. A dot on a tab shows that its conversations have unread messages. */
void updateTabBadges(Context* ctx) {
    lvgl_tabs_set_tab_badge(ctx->tabs, static_cast<uint32_t>(ViewMode::Channels), sumUnread(ctx->channelUnread) > 0);
    lvgl_tabs_set_tab_badge(ctx->tabs, static_cast<uint32_t>(ViewMode::Contacts), sumUnread(ctx->contactUnread) > 0);
}

/** An icon button for the header */
lv_obj_t* createHeaderButton(lv_obj_t* parent, const char* icon, lv_event_cb_t callback, Context* ctx) {
    auto* button = lvgl_icon_button_create(parent);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, ctx);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    return button;
}

/**
 * Replaces the toolbar of the tabs: the close button, the tabs next to it and the actions.
 * The tabs and the actions are grid navigation groups of their own, as grid navigation can't be nested.
 */
void createTabsHeader(lv_obj_t* parent, Context* ctx) {
    auto* header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const auto spacing = lvgl_theme_lerp(LV_DPX(2), LV_DPX(8), lvgl_theme_get_spacing());
    lv_obj_set_style_pad_hor(header, spacing, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(header, spacing, LV_STATE_DEFAULT);

    createHeaderButton(header, LVGL_ICON_SHARED_CLOSE, onBackPressed, ctx);

    ctx->tabs = lvgl_tabs_create(header);
    lvgl_tabs_set_mode(ctx->tabs, LVGL_TABS_MODE_WRAP);
    lvgl_tabs_add_tab(ctx->tabs, tr("Channels"));
    lvgl_tabs_add_tab(ctx->tabs, tr("Contacts"));
    lvgl_tabs_set_selected(ctx->tabs, static_cast<uint32_t>(ctx->mode));
    updateTabBadges(ctx);
    lv_obj_add_event_cb(ctx->tabs, onTabChanged, LV_EVENT_VALUE_CHANGED, ctx);

    // Pushes the actions to the right
    auto* spacer = lv_obj_create(header);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 0, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_clickable(spacer, false);

    auto* actions = lv_obj_create(header);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, spacing, LV_STATE_DEFAULT);
    // Both tabs have the same actions, so the tabs get the same room and keep their font when switching
    ctx->addButton = createHeaderButton(actions, LVGL_ICON_SHARED_ADD, onAddPressed, ctx);
    createHeaderButton(actions, LVGL_ICON_SHARED_SETTINGS, onSettingsPressed, ctx);
    createHeaderButton(actions, LVGL_ICON_SHARED_MORE_VERT, onMenuPressed, ctx);
    lvgl_grid_navigation_add(actions);
}

/** Requires the LVGL lock */
void updateWidgets(Context* ctx) {
    if (ctx->content == nullptr) {
        return;
    }
    updateNetworkHeader(ctx->networkHeader, ctx->network);
    if (ctx->tabs != nullptr) {
        updateTabBadges(ctx);
    }

    lv_obj_clean(ctx->content);
    if (ctx->mode == ViewMode::Inspect) {
        for (const auto& line : ctx->inspectLines) {
            addInspectLabel(ctx, line);
        }
        scrollInspectToNewest(ctx);
        return;
    }
    auto* list = createCard(ctx->content);
    if (ctx->mode == ViewMode::Channels) {
        // Disabled instead of hidden, so the header keeps its width
        lv_obj_set_state(ctx->addButton, LV_STATE_DISABLED, !ctx->canAddChannel);
        for (size_t i = 0; i < ctx->channels.size(); i++) {
            addListButton(list, ctx->channels[i].name, onItemPressed, ctx, (intptr_t)i, i < ctx->channelUnread.size() ? ctx->channelUnread[i] : 0);
        }
    } else {
        for (size_t i = 0; i < ctx->contacts.size(); i++) {
            addListButton(list, ctx->contacts[i].name, onItemPressed, ctx, (intptr_t)i, i < ctx->contactUnread.size() ? ctx->contactUnread[i] : 0);
        }
        if (ctx->contacts.empty()) {
            lvgl_list_add_text(list, tr("No contacts yet. Add a node that you've heard, or wait for a direct message."));
        }
    }
    lvgl_list_hide_last_divider(list);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->root = parent;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    if (ctx->mode == ViewMode::Inspect) {
        auto* toolbar = lvgl_toolbar_create(parent, tr("Inspect"));
        lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_LEFT, onBackPressed, ctx);
    } else {
        createTabsHeader(parent, ctx);
    }

    ctx->networkHeader = createNetworkHeader(parent);
    ctx->content = createContent(parent);
    if (ctx->mode != ViewMode::Inspect) {
        // The margin of the list's card is the space below the statusline, as much as the statusline has above its text
        lv_obj_set_style_pad_bottom(ctx->networkHeader, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(ctx->content, 0, LV_STATE_DEFAULT);
    }

    updateWidgets(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->root = nullptr;
    ctx->networkHeader = nullptr;
    ctx->content = nullptr;
    ctx->addButton = nullptr;
    ctx->tabs = nullptr;
}

/** Requires the LVGL lock */
void rebuildWidgets(Context* ctx) {
    if (ctx->root == nullptr) {
        return;
    }
    auto* root = ctx->root;
    lv_obj_clean(root);
    ctx->networkHeader = nullptr;
    ctx->content = nullptr;
    ctx->addButton = nullptr;
    ctx->tabs = nullptr;
    createWidgets(root, ctx);
}

void setMode(Context* ctx, ViewMode mode) {
    lvgl_lock();
    if (mode != ViewMode::Inspect) {
        ctx->lastTab = mode;
    }
    ctx->mode = mode;
    rebuildWidgets(ctx);
    lvgl_unlock();
}

::std::string formatSignal(float rssi, float snr) {
    char text[40];
    if (::std::isnan(snr)) {
        snprintf(text, sizeof(text), "%.0f dBm", rssi);
    } else {
        snprintf(text, sizeof(text), "%.0f dBm, %.1f dB", rssi, snr);
    }
    return text;
}

const char* getDeliveryName(MeshDeliveryState delivery) {
    switch (delivery) {
        case MESH_DELIVERY_QUEUED: return "queued";
        case MESH_DELIVERY_BLOCKED: return "blocked";
        case MESH_DELIVERY_SENT: return "sent";
        case MESH_DELIVERY_RETRYING: return "retrying";
        case MESH_DELIVERY_REPEATED: return "repeated";
        case MESH_DELIVERY_DELIVERED: return "delivered";
        case MESH_DELIVERY_FAILED: return "failed";
        case MESH_DELIVERY_CANCELLED: return "cancelled";
        default: return "";
    }
}

/** Queries the service, so it's called without the LVGL lock */
::std::string formatMessage(const Context* ctx, uint32_t seq) {
    auto* message = tt::newObject<MeshMessage>();
    if (message == nullptr) {
        return "";
    }
    ::std::string line;
    if (mesh_message_get(seq, message) == ERROR_NONE) {
        ::std::string target = addressToHex(message->peer).substr(0, 8);
        if (message->peer.length == 0) {
            target = "channel " + ::std::to_string(message->channel);
            for (const auto& channel : ctx->channels) {
                if (channel.index == message->channel) {
                    target = channel.name;
                }
            }
        }
        if (message->outgoing) {
            line = "> " + target + ": " + message->text;
        } else {
            line = ::std::string("< ") + (message->sender_name[0] != '\0' ? message->sender_name : "?") + " (" + target + ", " +
                formatSignal(message->rssi, message->snr) + "): " + message->text;
        }
    }
    tt::deleteObject(message);
    return line;
}

/** Frames that couldn't be read are encrypted for a key that this node doesn't have */
::std::string formatEvent(const Context* ctx, const MeshEvent& event) {
    switch (event.type) {
        case MESH_EVENT_MESSAGE:
            return formatMessage(ctx, event.seq);
        case MESH_EVENT_FRAME:
            if (event.frame.kind == MESH_FRAME_ADVERT) {
                return "< advert " + addressToHex(event.frame.from).substr(0, 8) + " (" + formatSignal(event.frame.rssi, event.frame.snr) + "): " +
                    (event.frame.name[0] != '\0' ? event.frame.name : "[no name]");
            }
            return "< (" + formatSignal(event.frame.rssi, event.frame.snr) + "): [encrypted: " + ::std::to_string(event.frame.length) + " bytes]";
        case MESH_EVENT_DELIVERY:
            return "#" + ::std::to_string(event.seq) + " " + getDeliveryName(event.delivery);
        case MESH_EVENT_STATE:
            return ::std::string("state: ") + mesh_state_to_string(event.state);
        default:
            return "";
    }
}

void addInspectLine(Context* ctx, ::std::string&& line) {
    lvgl_lock();
    ctx->inspectLines.push_back(tt::std::string(line.c_str()));
    const bool trimmed = ctx->inspectLines.size() > INSPECT_LINE_LIMIT;
    if (trimmed) {
        ctx->inspectLines.pop_front();
    }
    if (ctx->mode == ViewMode::Inspect && ctx->content != nullptr) {
        if (trimmed) {
            lv_obj_delete(lv_obj_get_child(ctx->content, 0));
        }
        addInspectLabel(ctx, ctx->inspectLines.back());
        scrollInspectToNewest(ctx);
    }
    lvgl_unlock();
}

void startConversation(Context* ctx) {
    const auto index = (size_t)ctx->openIndex.load();
    if (ctx->mode == ViewMode::Channels && index < ctx->channels.size()) {
        const ::std::string channelIndex = ::std::to_string(ctx->channels[index].index);
        const char* argv[] = { KIND_CHANNEL, channelIndex.c_str(), ctx->channels[index].name };
        startApp(CONVERSATION_APP_ID, 3, argv);
    } else if (ctx->mode == ViewMode::Contacts && index < ctx->contacts.size()) {
        const ::std::string address = addressToHex(ctx->contacts[index].address);
        const char* argv[] = { KIND_CONTACT, address.c_str(), ctx->contacts[index].name };
        startApp(CONVERSATION_APP_ID, 3, argv);
    }
}

void showMessage(Context* ctx, const char* title, const char* message) {
    if (ctx->messageDialogId == 0) {
        ctx->messageDialogId = alertdialog::start(ctx->appInstanceId, title, message);
    }
}

enum class AddType { JoinPublic, JoinPrivate, CreatePrivate };

/** Only offers what the mesh network supports */
void startAddChannel(Context* ctx) {
    if (ctx->addTypeDialogId != 0) {
        return;
    }
    ctx->addTypes.clear();
    ::std::vector<::std::string> items;
    if (mesh_supports(MESH_FEATURE_NAMED_CHANNELS)) {
        ctx->addTypes.push_back(static_cast<int>(AddType::JoinPublic));
        items.emplace_back(tr("Join public channel"));
    }
    if (mesh_supports(MESH_FEATURE_KEYED_CHANNELS)) {
        ctx->addTypes.push_back(static_cast<int>(AddType::JoinPrivate));
        items.emplace_back(tr("Join private channel"));
        ctx->addTypes.push_back(static_cast<int>(AddType::CreatePrivate));
        items.emplace_back(tr("Create private channel"));
    }
    if (items.empty()) {
        return;
    }
    ctx->addTypeDialogId = selectiondialog::start(ctx->appInstanceId, tr("Add channel"), items);
}

/** The order of these matches the options of the selection dialog */
enum class AddContactType { ByKey, Seen };

void startAddContactMenu(Context* ctx) {
    if (ctx->addContactDialogId != 0) {
        return;
    }
    ctx->addContactDialogId = selectiondialog::start(ctx->appInstanceId, tr("Add contact"), { tr("Add by key"), tr("Add seen") });
}

void startJoinChannel(Context* ctx);
void startJoinPrivateChannel(Context* ctx);
void startCreateChannel(Context* ctx);

void onAddTypeSelected(Context* ctx, int32_t selected) {
    if (selected < 0 || selected >= static_cast<int32_t>(ctx->addTypes.size())) {
        return;
    }
    switch (static_cast<AddType>(ctx->addTypes[selected])) {
        case AddType::JoinPublic:
            startJoinChannel(ctx);
            break;
        case AddType::JoinPrivate:
            startJoinPrivateChannel(ctx);
            break;
        case AddType::CreatePrivate:
            startCreateChannel(ctx);
            break;
    }
}

void startJoinChannel(Context* ctx) {
    if (ctx->joinChannelDialogId != 0) {
        return;
    }
    ctx->joinChannelDialogId = inputdialog::start(
        ctx->appInstanceId,
        tr("Join channel"),
        tr("Hashtag channel name, e.g. #test. To join Public, delete the # first."),
        "#",
        ctx->channelStream,
        ctx->channelBuffer,
        sizeof(ctx->channelBuffer),
        ctx->eventGroup
    );
}

void onJoinChannelResult(Context* ctx, int32_t result) {
    if (result == 0) {
        char name[sizeof(Context::channelBuffer)] = {};
        const size_t length = app_stream_read(&ctx->channelStream, name, sizeof(name) - 1);
        name[length] = '\0';
        const error_t error = meshsettings::joinHashtagChannel(name);
        if (error == ERROR_INVALID_ARGUMENT) {
            showMessage(ctx, tr("Couldn't join channel"), tr("Use a channel name that starts with # or Public. This mesh network can't join a channel by name."));
        } else if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to join channel %s: %s", name, error_to_string(error));
            showMessage(ctx, tr("Couldn't join channel"), error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
        }
    }
    app_stream_unsubscribe(&ctx->channelStream);
}

void startCreateChannel(Context* ctx) {
    if (ctx->createChannelDialogId != 0) {
        return;
    }
    ctx->createChannelDialogId = inputdialog::start(
        ctx->appInstanceId,
        tr("Create private channel"),
        tr("Name of the channel"),
        "",
        ctx->createStream,
        ctx->createBuffer,
        sizeof(ctx->createBuffer),
        ctx->eventGroup
    );
}

void onCreateChannelResult(Context* ctx, int32_t result) {
    if (result == 0) {
        char name[sizeof(Context::createBuffer)] = {};
        const size_t length = app_stream_read(&ctx->createStream, name, sizeof(name) - 1);
        name[length] = '\0';
        const error_t error = meshsettings::createPrivateChannel(name);
        if (error == ERROR_INVALID_ARGUMENT) {
            showMessage(ctx, tr("Couldn't create channel"), tr("Enter a name for the channel."));
        } else if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to create channel %s: %s", name, error_to_string(error));
            showMessage(ctx, tr("Couldn't create channel"), error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
        }
    }
    app_stream_unsubscribe(&ctx->createStream);
}

void startJoinPrivateChannel(Context* ctx) {
    if (ctx->privateNameDialogId != 0 || ctx->privateKeyDialogId != 0) {
        return;
    }
    ctx->privateNameDialogId = inputdialog::start(
        ctx->appInstanceId,
        tr("Join private channel"),
        tr("Name of the channel"),
        "",
        ctx->privateNameStream,
        ctx->privateNameBuffer,
        sizeof(ctx->privateNameBuffer),
        ctx->eventGroup
    );
}

void onPrivateNameResult(Context* ctx, int32_t result) {
    if (result == 0) {
        char name[sizeof(Context::privateNameBuffer)] = {};
        const size_t length = app_stream_read(&ctx->privateNameStream, name, sizeof(name) - 1);
        name[length] = '\0';
        ctx->pendingChannelName = name;
    }
    app_stream_unsubscribe(&ctx->privateNameStream);
    if (result != 0 || ctx->pendingChannelName.empty()) {
        ctx->pendingChannelName.clear();
        return;
    }
    ctx->privateKeyDialogId = inputdialog::start(
        ctx->appInstanceId,
        tr("Channel key"),
        tr("Key of the channel in hexadecimal"),
        "",
        ctx->privateKeyStream,
        ctx->privateKeyBuffer,
        sizeof(ctx->privateKeyBuffer),
        ctx->eventGroup
    );
}

void onPrivateKeyResult(Context* ctx, int32_t result) {
    if (result == 0) {
        char key[sizeof(Context::privateKeyBuffer)] = {};
        const size_t length = app_stream_read(&ctx->privateKeyStream, key, sizeof(key) - 1);
        key[length] = '\0';
        MeshAddress bytes;
        if (!hexToAddress(key, &bytes)) {
            showMessage(ctx, tr("Invalid key"), tr("Enter the key as hexadecimal characters, two for every byte."));
        } else {
            const error_t error = meshsettings::joinPrivateChannel(ctx->pendingChannelName.c_str(), bytes.bytes, bytes.length);
            if (error == ERROR_INVALID_ARGUMENT) {
                showMessage(ctx, tr("Invalid key"), tr("This mesh network expects a key of a different length."));
            } else if (error != ERROR_NONE) {
                LOG_E(TAG, "Failed to join channel %s: %s", ctx->pendingChannelName.c_str(), error_to_string(error));
                showMessage(ctx, tr("Couldn't join channel"), error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
            }
        }
    }
    app_stream_unsubscribe(&ctx->privateKeyStream);
    ctx->pendingChannelName.clear();
}

void startAddContact() {
    startApp(ADD_CONTACT_APP_ID, 0, nullptr);
}

/** Shows this node as a QR code that other mesh apps can scan */
void showOwnQr(Context* ctx) {
    char uri[256];
    const error_t error = mesh_get_contact_uri(nullptr, uri, sizeof(uri));
    if (error == ERROR_NONE) {
        qrcode::start(uri);
    } else if (error == ERROR_NOT_SUPPORTED) {
        showMessage(ctx, tr("Not available"), tr("This mesh network can't share a contact as a QR code."));
    } else {
        LOG_E(TAG, "Failed to get the contact: %s", error_to_string(error));
        showMessage(ctx, tr("Couldn't create QR code"), error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
    }
}

void startContactNameDialog(Context* ctx) {
    if (ctx->contactNameDialogId != 0 || ctx->contactKeyDialogId != 0) {
        return;
    }
    ctx->contactNameDialogId = inputdialog::start(
        ctx->appInstanceId,
        tr("Add contact"),
        tr("Name of the contact"),
        "",
        ctx->contactNameStream,
        ctx->contactNameBuffer,
        sizeof(ctx->contactNameBuffer),
        ctx->eventGroup
    );
}

void onContactNameResult(Context* ctx, int32_t result) {
    if (result == 0) {
        char name[sizeof(Context::contactNameBuffer)] = {};
        const size_t length = app_stream_read(&ctx->contactNameStream, name, sizeof(name) - 1);
        name[length] = '\0';
        ctx->pendingContactName = name;
    }
    app_stream_unsubscribe(&ctx->contactNameStream);
    if (result != 0 || ctx->pendingContactName.empty()) {
        ctx->pendingContactName.clear();
        return;
    }
    ctx->contactKeyDialogId = inputdialog::start(
        ctx->appInstanceId,
        tr("Public key"),
        tr("Public key of the contact in hexadecimal"),
        "",
        ctx->contactKeyStream,
        ctx->contactKeyBuffer,
        sizeof(ctx->contactKeyBuffer),
        ctx->eventGroup
    );
}

void onContactKeyResult(Context* ctx, int32_t result) {
    if (result == 0) {
        char key[sizeof(Context::contactKeyBuffer)] = {};
        const size_t length = app_stream_read(&ctx->contactKeyStream, key, sizeof(key) - 1);
        key[length] = '\0';
        MeshAddress address;
        if (!hexToAddress(key, &address)) {
            showMessage(ctx, tr("Invalid key"), tr("Enter the public key as hexadecimal characters, two for every byte."));
        } else {
            const error_t error = mesh_add_contact(&address, ctx->pendingContactName.c_str());
            if (error == ERROR_INVALID_ARGUMENT) {
                showMessage(ctx, tr("Invalid key"), tr("This mesh network expects a public key of a different length."));
            } else if (error != ERROR_NONE) {
                LOG_E(TAG, "Failed to add contact: %s", error_to_string(error));
                showMessage(ctx, tr("Couldn't add contact"), error == ERROR_RESOURCE ? tr("There are too many contacts.") : error_to_string(error));
            }
        }
    }
    app_stream_unsubscribe(&ctx->contactKeyStream);
    ctx->pendingContactName.clear();
}

enum class MenuItem { SetPath, ResetPath, MarkAllRead, AdvertZeroHop, AdvertFlood, ShowQr, Info, Inspect };

/** The contacts tab also has the path actions for contacts */
void startMenu(Context* ctx) {
    if (ctx->menuDialogId != 0) {
        return;
    }
    ctx->menuItems.clear();
    ::std::vector<::std::string> items;
    const auto add = [ctx, &items](MenuItem item, const char* text) {
        ctx->menuItems.push_back(static_cast<int>(item));
        items.emplace_back(text);
    };
    if (ctx->mode == ViewMode::Contacts && mesh_supports(MESH_FEATURE_CONTACT_PATHS)) {
        add(MenuItem::SetPath, tr("Set path"));
        add(MenuItem::ResetPath, tr("Reset path"));
    }
    add(MenuItem::MarkAllRead, tr("Mark all as read"));
    add(MenuItem::AdvertZeroHop, tr("Advert: Zero hop"));
    add(MenuItem::AdvertFlood, tr("Advert: Flood routed"));
    add(MenuItem::ShowQr, tr("Show QR"));
    add(MenuItem::Info, tr("Info"));
    add(MenuItem::Inspect, tr("Inspect"));
    ctx->menuDialogId = selectiondialog::start(ctx->appInstanceId, tr("Mesh"), items);
}

/** The hashes of the repeaters as hexadecimal tokens of one length, separated by commas or spaces, e.g. "a1,b2,c3" */
bool parsePath(const char* text, ::std::vector<uint8_t>& hops, size_t* hashSize) {
    hops.clear();
    *hashSize = 0;
    const char* position = text;
    while (*position != '\0') {
        while (*position == ',' || *position == ' ') {
            position++;
        }
        const char* end = position;
        while (*end != '\0' && *end != ',' && *end != ' ') {
            end++;
        }
        const size_t length = end - position;
        if (length == 0) {
            break;
        }
        if (length % 2 != 0 || length / 2 > 4) {
            return false;
        }
        if (*hashSize == 0) {
            *hashSize = length / 2;
        } else if (length / 2 != *hashSize) {
            return false;
        }
        for (size_t i = 0; i < length; i += 2) {
            char pair[3] = { position[i], position[i + 1], '\0' };
            char* pairEnd = nullptr;
            hops.push_back(static_cast<uint8_t>(strtoul(pair, &pairEnd, 16)));
            if (pairEnd != pair + 2) {
                return false;
            }
        }
        position = end;
    }
    return !hops.empty() && hops.size() / *hashSize <= 63 && hops.size() <= 64;
}

void startPathContactSelection(Context* ctx, bool reset) {
    if (ctx->pathContactDialogId != 0 || ctx->pathInputDialogId != 0) {
        return;
    }
    if (ctx->contacts.empty()) {
        showMessage(ctx, tr("No contacts"), tr("Add a contact first."));
        return;
    }
    ctx->pathReset = reset;
    ::std::vector<::std::string> names;
    ctx->pathContactChoices.clear();
    for (const auto& contact : ctx->contacts) {
        names.emplace_back(contact.name);
        ctx->pathContactChoices.push_back(contact.address);
    }
    ctx->pathContactDialogId = selectiondialog::start(ctx->appInstanceId, reset ? tr("Reset path") : tr("Set path"), names);
}

void showPathError(Context* ctx, error_t error) {
    LOG_E(TAG, "Failed to change the path: %s", error_to_string(error));
    showMessage(ctx, tr("Couldn't change path"), error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
}

void onPathContactSelected(Context* ctx, int32_t selected) {
    // The service tells when the contact doesn't exist anymore
    if (selected < 0 || selected >= static_cast<int32_t>(ctx->pathContactChoices.size())) {
        return;
    }
    ctx->pathContact = ctx->pathContactChoices[selected];
    if (ctx->pathReset) {
        const error_t error = mesh_reset_contact_path(&ctx->pathContact);
        if (error != ERROR_NONE) {
            showPathError(ctx, error);
        }
        return;
    }
    ctx->pathInputDialogId = inputdialog::start(
        ctx->appInstanceId,
        tr("Set path"),
        tr("Repeater hashes in hexadecimal, e.g. a1,b2,c3"),
        "",
        ctx->pathStream,
        ctx->pathBuffer,
        sizeof(ctx->pathBuffer),
        ctx->eventGroup
    );
}

void onPathInputResult(Context* ctx, int32_t result) {
    if (result == 0) {
        char text[sizeof(Context::pathBuffer)] = {};
        const size_t length = app_stream_read(&ctx->pathStream, text, sizeof(text) - 1);
        text[length] = '\0';
        ::std::vector<uint8_t> hops;
        size_t hashSize = 0;
        if (!parsePath(text, hops, &hashSize)) {
            showMessage(ctx, tr("Invalid path"), tr("Enter the hashes of the repeaters as hexadecimal characters, all of the same length, separated by commas."));
        } else {
            const error_t error = mesh_set_contact_path(&ctx->pathContact, hops.data(), hops.size() / hashSize, hashSize);
            if (error == ERROR_INVALID_ARGUMENT) {
                showMessage(ctx, tr("Invalid path"), tr("The path is too long."));
            } else if (error != ERROR_NONE) {
                showPathError(ctx, error);
            }
        }
    }
    app_stream_unsubscribe(&ctx->pathStream);
}

void sendAdvert(Context* ctx, MeshAdvertRoute route) {
    const error_t error = mesh_send_advert(route);
    if (error != ERROR_NONE && ctx->messageDialogId == 0) {
        const char* reason = error == ERROR_INVALID_STATE ? tr("Turn mesh on to send an advert") : (error == ERROR_NOT_SUPPORTED ? tr("This mesh network has no adverts.") : error_to_string(error));
        ctx->messageDialogId = alertdialog::start(ctx->appInstanceId, tr("Couldn't send advert"), reason);
    }
}

void onAddContactSelected(Context* ctx, int32_t selected) {
    if (static_cast<AddContactType>(selected) == AddContactType::ByKey) {
        startContactNameDialog(ctx);
    } else if (static_cast<AddContactType>(selected) == AddContactType::Seen) {
        startAddContact();
    }
}

void onMenuSelected(Context* ctx, int32_t selected) {
    if (selected < 0 || selected >= static_cast<int32_t>(ctx->menuItems.size())) {
        return;
    }
    switch (static_cast<MenuItem>(ctx->menuItems[selected])) {
        case MenuItem::SetPath:
            startPathContactSelection(ctx, false);
            break;
        case MenuItem::ResetPath:
            startPathContactSelection(ctx, true);
            break;
        case MenuItem::MarkAllRead:
            mesh_mark_all_read();
            break;
        case MenuItem::AdvertZeroHop:
            sendAdvert(ctx, MESH_ADVERT_ZERO_HOP);
            break;
        case MenuItem::AdvertFlood:
            sendAdvert(ctx, MESH_ADVERT_FLOOD);
            break;
        case MenuItem::ShowQr:
            showOwnQr(ctx);
            break;
        case MenuItem::Info:
            startApp(MESH_INFO_APP_ID, 0, nullptr);
            break;
        case MenuItem::Inspect:
            setMode(ctx, ViewMode::Inspect);
            break;
    }
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    ctx.eventGroup = &eventGroup;
    check(task_event_group_claim_bit(&eventGroup, &ctx.openBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.menuBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.settingsBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.addBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.showChannelsBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.showContactsBit) == ERROR_NONE);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);
    MeshEventSubscription meshEvents {};
    check(mesh_event_subscribe(&meshEvents, &eventGroup) == ERROR_NONE);

    {
        Snapshot snapshot = loadData();
        lvgl_lock();
        commitData(&ctx, ::std::move(snapshot));
        lvgl_unlock();
    }
    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, portMAX_DELAY);

        if (flags & ctx.openBit) {
            startConversation(&ctx);
        }
        if (flags & ctx.menuBit) {
            startMenu(&ctx);
        }
        if (flags & ctx.settingsBit) {
            startApp(MESH_SETTINGS_APP_ID, 0, nullptr);
        }
        if (flags & ctx.addBit) {
            if (ctx.mode == ViewMode::Contacts) {
                startAddContactMenu(&ctx);
            } else {
                startAddChannel(&ctx);
            }
        }
        if ((flags & ctx.showChannelsBit) && ctx.mode != ViewMode::Channels) {
            setMode(&ctx, ViewMode::Channels);
        }
        if ((flags & ctx.showContactsBit) && ctx.mode != ViewMode::Contacts) {
            setMode(&ctx, ViewMode::Contacts);
        }

        bool changed = false;
        MeshEvent meshEvent {};
        while (mesh_event_poll(&meshEvents, &meshEvent) == ERROR_NONE) {
            changed |= meshEvent.type == MESH_EVENT_STATE || meshEvent.type == MESH_EVENT_CHANNEL || meshEvent.type == MESH_EVENT_CONTACT ||
                meshEvent.type == MESH_EVENT_MESSAGE || meshEvent.type == MESH_EVENT_UNREAD;
            ::std::string line = formatEvent(&ctx, meshEvent);
            if (!line.empty()) {
                addInspectLine(&ctx, ::std::move(line));
            }
        }

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                // Inspect returns to the tabs, which close the app
                if (ctx.mode != ViewMode::Inspect) {
                    shouldClose = true;
                    break;
                }
                setMode(&ctx, ctx.lastTab);
            }
            if (event.type == APP_EVENT_RESULT) {
                if (event.result.launch_id == ctx.menuDialogId) {
                    ctx.menuDialogId = 0;
                    if (event.result.result >= 0) {
                        onMenuSelected(&ctx, event.result.result);
                    }
                } else if (event.result.launch_id == ctx.addContactDialogId) {
                    ctx.addContactDialogId = 0;
                    if (event.result.result >= 0) {
                        onAddContactSelected(&ctx, event.result.result);
                    }
                } else if (event.result.launch_id == ctx.addTypeDialogId) {
                    ctx.addTypeDialogId = 0;
                    if (event.result.result >= 0) {
                        onAddTypeSelected(&ctx, event.result.result);
                    }
                } else if (event.result.launch_id == ctx.privateNameDialogId) {
                    ctx.privateNameDialogId = 0;
                    onPrivateNameResult(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.privateKeyDialogId) {
                    ctx.privateKeyDialogId = 0;
                    onPrivateKeyResult(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.createChannelDialogId) {
                    ctx.createChannelDialogId = 0;
                    onCreateChannelResult(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.joinChannelDialogId) {
                    ctx.joinChannelDialogId = 0;
                    onJoinChannelResult(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.contactNameDialogId) {
                    ctx.contactNameDialogId = 0;
                    onContactNameResult(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.contactKeyDialogId) {
                    ctx.contactKeyDialogId = 0;
                    onContactKeyResult(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.pathContactDialogId) {
                    ctx.pathContactDialogId = 0;
                    onPathContactSelected(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.pathInputDialogId) {
                    ctx.pathInputDialogId = 0;
                    onPathInputResult(&ctx, event.result.result);
                } else if (event.result.launch_id == ctx.messageDialogId) {
                    ctx.messageDialogId = 0;
                }
                app_manager_stop(event.result.launch_id);
                // The channels and contacts may have changed, and the window may have been rebuilt
                changed = true;
            }
        }

        if (changed) {
            Snapshot snapshot = loadData();
            lvgl_lock();
            commitData(&ctx, ::std::move(snapshot));
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

extern const ::AppManifest manifest = {
    .id = "tactility.mesh",
    .name = "Mesh",
    .category = APP_CATEGORY_USER,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = { .depth = 7168, .desired_memory_capability = 0 },
};

} // namespace tt::app::meshchat
