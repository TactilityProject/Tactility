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

#include <tactility/std/vector.hpp>

#include <mesh/mesh.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace tt::app::meshchat {

extern const ::AppManifest manifest;

namespace {

constexpr auto* TAG = "Mesh";
constexpr size_t CHANNEL_LIMIT = 8;
constexpr size_t CONTACT_LIMIT = 64;

enum class ViewMode { Menu, Contacts, Channels };

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t openBit = 0;
    uint32_t menuBit = 0;
    uint32_t settingsBit = 0;
    uint32_t addChannelBit = 0;
    uint32_t showChannelsBit = 0;
    uint32_t showContactsBit = 0;
    ViewMode mode = ViewMode::Menu;
    /** Written by the LVGL thread before it signals openBit */
    ::std::atomic<int32_t> openIndex = 0;
    uint32_t menuDialogId = 0;
    uint32_t addTypeDialogId = 0;
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
    // Valid while the window is shown
    lv_obj_t* root = nullptr;
    lv_obj_t* networkHeader = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* addButton = nullptr;
    NetworkInfo network;
};

/** Queries the service thread, so it's called without the LVGL lock */
void loadData(Context* ctx) {
    ctx->network = loadNetworkInfo();
    ctx->canAddChannel = mesh_supports(MESH_FEATURE_NAMED_CHANNELS) || mesh_supports(MESH_FEATURE_KEYED_CHANNELS);
    ctx->channels.assign(CHANNEL_LIMIT, MeshChannel {});
    size_t count = 0;
    if (mesh_get_channels(ctx->channels.data(), CHANNEL_LIMIT, &count) != ERROR_NONE) {
        count = 0;
    }
    ctx->channels.resize(count);
    ctx->contacts.assign(CONTACT_LIMIT, MeshContact {});
    count = 0;
    if (mesh_get_contacts(ctx->contacts.data(), CONTACT_LIMIT, &count) != ERROR_NONE) {
        count = 0;
    }
    ctx->contacts.resize(count);
}

lv_obj_t* addListButton(lv_obj_t* list, const char* text, lv_event_cb_t callback, void* callbackData, intptr_t index) {
    auto* button = lvgl_list_add_button(list, nullptr, text);
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

void onAddChannelPressed(lv_event_t* event) { signalBit(event, &Context::addChannelBit); }
void onMenuPressed(lv_event_t* event) { signalBit(event, &Context::menuBit); }
void onSettingsPressed(lv_event_t* event) { signalBit(event, &Context::settingsBit); }
void onShowChannelsPressed(lv_event_t* event) { signalBit(event, &Context::showChannelsBit); }
void onShowContactsPressed(lv_event_t* event) { signalBit(event, &Context::showContactsBit); }

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

/** Requires the LVGL lock */
void updateWidgets(Context* ctx) {
    if (ctx->content == nullptr) {
        return;
    }
    if (ctx->mode == ViewMode::Menu) {
        return;
    }
    updateNetworkHeader(ctx->networkHeader, ctx->network);

    lv_obj_clean(ctx->content);
    auto* list = createCard(ctx->content);
    if (ctx->mode == ViewMode::Channels) {
        lv_obj_set_hidden(ctx->addButton, !ctx->canAddChannel);
        for (size_t i = 0; i < ctx->channels.size(); i++) {
            addListButton(list, ctx->channels[i].name, onItemPressed, ctx, (intptr_t)i);
        }
    } else {
        for (size_t i = 0; i < ctx->contacts.size(); i++) {
            addListButton(list, ctx->contacts[i].name, onItemPressed, ctx, (intptr_t)i);
        }
        if (ctx->contacts.empty()) {
            lvgl_list_add_text(list, "No contacts yet. Add a node that you've heard, or wait for a direct message.");
        }
    }
    lvgl_list_hide_last_divider(list);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->root = parent;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    const char* title = ctx->mode == ViewMode::Channels ? "Channels" : ctx->mode == ViewMode::Contacts ? "Contacts" : "Mesh";
    auto* toolbar = lvgl_toolbar_create(parent, title);
    lvgl_toolbar_set_nav_action(toolbar, ctx->mode == ViewMode::Menu ? LV_SYMBOL_CLOSE : LV_SYMBOL_LEFT, onBackPressed, ctx);

    if (ctx->mode == ViewMode::Menu) {
        lvgl_toolbar_add_text_button_action(toolbar, LV_SYMBOL_SETTINGS, onSettingsPressed, ctx);
        auto* menuContent = createContent(parent);
        ctx->content = menuContent;
        auto* list = createCard(menuContent);
        addListButton(list, "Channels", onShowChannelsPressed, ctx, 0);
        addListButton(list, "Contacts", onShowContactsPressed, ctx, 0);
        lvgl_list_hide_last_divider(list);
        return;
    }

    if (ctx->mode == ViewMode::Channels) {
        ctx->addButton = lvgl_toolbar_add_text_button_action(toolbar, LV_SYMBOL_PLUS, onAddChannelPressed, ctx);
    } else {
        auto* menuButton = lvgl_toolbar_add_text_button_action(toolbar, LVGL_ICON_SHARED_MORE_VERT, onMenuPressed, ctx);
        lv_obj_set_style_text_font(menuButton, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    }

    ctx->networkHeader = createNetworkHeader(parent);
    ctx->content = createContent(parent);

    updateWidgets(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->root = nullptr;
    ctx->networkHeader = nullptr;
    ctx->content = nullptr;
    ctx->addButton = nullptr;
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
    createWidgets(root, ctx);
}

void setMode(Context* ctx, ViewMode mode) {
    lvgl_lock();
    ctx->mode = mode;
    rebuildWidgets(ctx);
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
        items.emplace_back("Join public channel");
    }
    if (mesh_supports(MESH_FEATURE_KEYED_CHANNELS)) {
        ctx->addTypes.push_back(static_cast<int>(AddType::JoinPrivate));
        items.emplace_back("Join private channel");
        ctx->addTypes.push_back(static_cast<int>(AddType::CreatePrivate));
        items.emplace_back("Create private channel");
    }
    if (items.empty()) {
        return;
    }
    ctx->addTypeDialogId = selectiondialog::start(ctx->appInstanceId, "Add channel", items);
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
        "Join channel",
        "Hashtag channel name, e.g. #test. To join Public, delete the # first.",
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
            showMessage(ctx, "Couldn't join channel", "Use a channel name that starts with # or Public. This mesh network can't join a channel by name.");
        } else if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to join channel %s: %s", name, error_to_string(error));
            showMessage(ctx, "Couldn't join channel", error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
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
        "Create private channel",
        "Name of the channel",
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
            showMessage(ctx, "Couldn't create channel", "Enter a name for the channel.");
        } else if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to create channel %s: %s", name, error_to_string(error));
            showMessage(ctx, "Couldn't create channel", error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
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
        "Join private channel",
        "Name of the channel",
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
        "Channel key",
        "Key of the channel in hexadecimal",
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
            showMessage(ctx, "Invalid key", "Enter the key as hexadecimal characters, two for every byte.");
        } else {
            const error_t error = meshsettings::joinPrivateChannel(ctx->pendingChannelName.c_str(), bytes.bytes, bytes.length);
            if (error == ERROR_INVALID_ARGUMENT) {
                showMessage(ctx, "Invalid key", "This mesh network expects a key of a different length.");
            } else if (error != ERROR_NONE) {
                LOG_E(TAG, "Failed to join channel %s: %s", ctx->pendingChannelName.c_str(), error_to_string(error));
                showMessage(ctx, "Couldn't join channel", error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
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
        showMessage(ctx, "Not available", "This mesh network can't share a contact as a QR code.");
    } else {
        LOG_E(TAG, "Failed to get the contact: %s", error_to_string(error));
        showMessage(ctx, "Couldn't create QR code", error == ERROR_INVALID_STATE ? getNotReadyText().c_str() : error_to_string(error));
    }
}

void startContactNameDialog(Context* ctx) {
    if (ctx->contactNameDialogId != 0 || ctx->contactKeyDialogId != 0) {
        return;
    }
    ctx->contactNameDialogId = inputdialog::start(
        ctx->appInstanceId,
        "Add contact",
        "Name of the contact",
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
        "Public key",
        "Public key of the contact in hexadecimal",
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
            showMessage(ctx, "Invalid key", "Enter the public key as hexadecimal characters, two for every byte.");
        } else {
            const error_t error = mesh_add_contact(&address, ctx->pendingContactName.c_str());
            if (error == ERROR_INVALID_ARGUMENT) {
                showMessage(ctx, "Invalid key", "This mesh network expects a public key of a different length.");
            } else if (error != ERROR_NONE) {
                LOG_E(TAG, "Failed to add contact: %s", error_to_string(error));
                showMessage(ctx, "Couldn't add contact", error == ERROR_RESOURCE ? "There are too many contacts." : error_to_string(error));
            }
        }
    }
    app_stream_unsubscribe(&ctx->contactKeyStream);
    ctx->pendingContactName.clear();
}

/** The order of these matches the options of the selection dialog */
enum class MenuItem { AddByKey, AddSeen, ShowQr };

void startMenu(Context* ctx) {
    if (ctx->menuDialogId != 0) {
        return;
    }
    ctx->menuDialogId = selectiondialog::start(ctx->appInstanceId, "Mesh Contacts", {
        "Add by key",
        "Add seen",
        "Show my QR"
    });
}

void onMenuSelected(Context* ctx, int32_t selected) {
    switch (static_cast<MenuItem>(selected)) {
        case MenuItem::AddByKey:
            startContactNameDialog(ctx);
            break;
        case MenuItem::AddSeen:
            startAddContact();
            break;
        case MenuItem::ShowQr:
            showOwnQr(ctx);
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
    check(task_event_group_claim_bit(&eventGroup, &ctx.addChannelBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.showChannelsBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.showContactsBit) == ERROR_NONE);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);
    MeshEventSubscription meshEvents {};
    check(mesh_event_subscribe(&meshEvents, &eventGroup) == ERROR_NONE);

    loadData(&ctx);
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
        if (flags & ctx.addChannelBit) {
            startAddChannel(&ctx);
        }
        if (flags & ctx.showChannelsBit) {
            setMode(&ctx, ViewMode::Channels);
        }
        if (flags & ctx.showContactsBit) {
            setMode(&ctx, ViewMode::Contacts);
        }

        bool changed = false;
        MeshEvent meshEvent {};
        while (mesh_event_poll(&meshEvents, &meshEvent) == ERROR_NONE) {
            changed |= meshEvent.type == MESH_EVENT_STATE || meshEvent.type == MESH_EVENT_CHANNEL || meshEvent.type == MESH_EVENT_CONTACT;
        }

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                if (ctx.mode == ViewMode::Menu) {
                    shouldClose = true;
                    break;
                }
                setMode(&ctx, ViewMode::Menu);
            }
            if (event.type == APP_EVENT_RESULT) {
                if (event.result.launch_id == ctx.menuDialogId) {
                    ctx.menuDialogId = 0;
                    if (event.result.result >= 0) {
                        onMenuSelected(&ctx, event.result.result);
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
                } else if (event.result.launch_id == ctx.messageDialogId) {
                    ctx.messageDialogId = 0;
                }
                app_manager_stop(event.result.launch_id);
                // The channels and contacts may have changed, and the window may have been rebuilt
                changed = true;
            }
        }

        if (changed) {
            loadData(&ctx);
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

extern const ::AppManifest manifest = {
    .id = "tactility.mesh",
    .name = "Mesh",
    .category = APP_CATEGORY_USER,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = { .depth = 6144, .desired_memory_capability = 0 },
};

} // namespace tt::app::meshchat
