// SPDX-License-Identifier: Apache-2.0
#define L10N_APP "softwaresettings"
#include <Tactility/i18n/AppDomain.h>

#include <Tactility/BuildInfo.h>
#include <Tactility/app/dialog/Dialog.h>
#include <Tactility/service/firmwareupdate/FirmwareUpdate.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <app_hub/app_hub.h>
#include <app_hub/app_hub_repositories_app.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/spinner.h>
#include <lvgl/widgets/toolbar.h>

#ifdef ESP_PLATFORM
#include <esp_system.h>
#endif

#include <atomic>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace tt::app::softwaresettings {

extern const ::AppManifest manifest;

namespace {

namespace firmwareupdate = service::firmwareupdate;

constexpr auto* TAG = "SoftwareSettings";

enum class Request : int32_t {
    None = 0,
    Check,
    Install,
    Restart,
    ForceUpdate,
    EnableDevelop,
    ChooseChannel,
    OpenRepositories,
};

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    /** Signaled by the widgets, which store the request first */
    uint32_t requestBit = 0;
    std::atomic<Request> request { Request::None };
    /** Signaled by the firmware update service when its state changes */
    uint32_t firmwareBit = 0;
    /** Signaled by the dialogs, which store their result in dialogResult first */
    uint32_t installDialogBit = 0;
    uint32_t restartDialogBit = 0;
    uint32_t forceDialogBit = 0;
    uint32_t developDialogBit = 0;
    uint32_t channelDialogBit = 0;
    std::atomic<int32_t> dialogResult { -1 };
    /** The commit of the release that the install dialog shows, so only that release is installed */
    char confirmCommit[sizeof(firmwareupdate::Release::gitCommit)] {};
    /** The restart is offered once per installed update while the app runs */
    bool restartOffered = false;
    /** An incompatible update is explained once per failed install */
    bool incompatibleExplained = false;
    /** Counts taps within a period. Only used with the LVGL lock held. */
    struct TapCounter {
        uint32_t count = 0;
        uint32_t firstTick = 0;
    };
    /** Taps on the status label, which unlock a forced update */
    TapCounter statusTaps;
    /** Taps on the channel label, which offer the develop channel */
    TapCounter channelTaps;
    /** The button offers a forced update instead of a check. Only used with the LVGL lock held. */
    bool forceUpdateUnlocked = false;
    /** What the widgets show. Written by the app's task with the LVGL lock held. */
    firmwareupdate::Snapshot snapshot {};
    size_t repositoryCount = 0;
    /** nullptr while the widgets don't exist */
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* detailLabel = nullptr;
    lv_obj_t* progressBar = nullptr;
    lv_obj_t* progressSpinner = nullptr;
    lv_obj_t* actionButton = nullptr;
    lv_obj_t* actionLabel = nullptr;
    lv_obj_t* channelLabel = nullptr;
    lv_obj_t* channelButton = nullptr;
    lv_obj_t* repositoriesLabel = nullptr;
};

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void signal(Context* ctx, Request request) {
    ctx->request = request;
    task_event_group_signal(ctx->eventGroup, ctx->requestBit);
}

bool canInstall(const firmwareupdate::Snapshot& snapshot) {
    return snapshot.installSupported && !snapshot.fullInstallRequired &&
        (snapshot.status == firmwareupdate::Status::Available || snapshot.status == firmwareupdate::Status::InstallFailed);
}

/** The button would check for updates, which a forced update can replace */
bool showsCheck(const firmwareupdate::Snapshot& snapshot) {
    const auto status = snapshot.status;
    return status != firmwareupdate::Status::Installed && status != firmwareupdate::Status::Checking &&
        status != firmwareupdate::Status::Installing && !canInstall(snapshot);
}

bool isForceUpdateShown(const Context* ctx) {
    return ctx->forceUpdateUnlocked && ctx->snapshot.installSupported && showsCheck(ctx->snapshot);
}

/** The button checks for updates, installs the available one, or restarts into the installed one */
void onActionPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->snapshot.status == firmwareupdate::Status::Installed) {
        signal(ctx, Request::Restart);
    } else if (canInstall(ctx->snapshot)) {
        signal(ctx, Request::Install);
    } else if (isForceUpdateShown(ctx)) {
        signal(ctx, Request::ForceUpdate);
    } else {
        signal(ctx, Request::Check);
    }
}

void updateFirmwareWidgets(Context* ctx);

constexpr uint32_t UNLOCK_TAPS = 5;
constexpr uint32_t UNLOCK_TAP_PERIOD_MS = 3000;

/** @return true at the 5th tap within 3 seconds */
bool countTap(Context::TapCounter& counter) {
    if (counter.count == 0 || lv_tick_elaps(counter.firstTick) > UNLOCK_TAP_PERIOD_MS) {
        counter.count = 0;
        counter.firstTick = lv_tick_get();
    }
    counter.count++;
    if (counter.count < UNLOCK_TAPS) {
        return false;
    }
    counter.count = 0;
    return true;
}

/** Tapping the status label 5 times within 3 seconds offers to install the published build even when it isn't newer, to test updating */
void onStatusPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (countTap(ctx->statusTaps)) {
        ctx->forceUpdateUnlocked = true;
        updateFirmwareWidgets(ctx);
    }
}

/** Tapping the channel label 5 times within 3 seconds offers the develop channel, which isn't listed otherwise */
void onChannelLabelPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (countTap(ctx->channelTaps) && ctx->snapshot.channel != firmwareupdate::Channel::Develop) {
        signal(ctx, Request::EnableDevelop);
    }
}

void onChannelPressed(lv_event_t* event) {
    signal(static_cast<Context*>(lv_event_get_user_data(event)), Request::ChooseChannel);
}

/** The channels that can be chosen, in order. Stable and Develop are only listed for a device that follows them already. */
constexpr firmwareupdate::Channel SELECTABLE_CHANNELS[] = { firmwareupdate::Channel::Stable, firmwareupdate::Channel::Unstable, firmwareupdate::Channel::Develop };

bool isChannelListed(firmwareupdate::Channel channel, firmwareupdate::Channel current) {
    return channel == firmwareupdate::Channel::Unstable || channel == current;
}

const char* getChannelName(firmwareupdate::Channel channel) {
    switch (channel) {
        case firmwareupdate::Channel::Stable: return tr("Main");
        case firmwareupdate::Channel::Unstable: return tr("Beta");
        case firmwareupdate::Channel::Develop: return tr("Develop");
    }
    return "";
}

void onRepositoriesPressed(lv_event_t* event) {
    signal(static_cast<Context*>(lv_event_get_user_data(event)), Request::OpenRepositories);
}

lv_obj_t* createRow(lv_obj_t* parent) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_style_pad_column(row, LV_DPX(8), LV_STATE_DEFAULT);
    return row;
}

/** A column that takes the free width of a row */
lv_obj_t* createTexts(lv_obj_t* row) {
    auto* texts = lv_obj_create(row);
    lv_obj_remove_style_all(texts);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(texts, false);
    return texts;
}

lv_obj_t* createLabel(lv_obj_t* parent, bool small) {
    auto* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    if (small) {
        lv_obj_set_style_text_font(label, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    }
    return label;
}

lv_obj_t* createTextButton(lv_obj_t* parent, const char* text, lv_event_cb_t callback, Context* ctx) {
    auto* button = lv_button_create(parent);
    lv_label_set_text(lv_label_create(button), text);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, ctx);
    return button;
}

std::string formatDate(int64_t timestamp) {
    const time_t time = static_cast<time_t>(timestamp);
    tm local {};
    char text[16] = "";
    if (localtime_r(&time, &local) != nullptr) {
        strftime(text, sizeof(text), "%Y-%m-%d", &local);
    }
    return text;
}

/** Requires the LVGL lock */
void updateFirmwareWidgets(Context* ctx) {
    if (ctx->statusLabel == nullptr) {
        return;
    }
    const auto& snapshot = ctx->snapshot;
    const char* status = tr("Up to date");
    const char* action = tr("Check");
    bool actionEnabled = true;
    std::string detail = i18n::format(tr("{} from {}"), { TT_VERSION, formatDate(tt_build_timestamp) });
    switch (snapshot.status) {
        case firmwareupdate::Status::Unknown:
        case firmwareupdate::Status::UpToDate:
            break;
        case firmwareupdate::Status::Checking:
            action = tr("Checking...");
            actionEnabled = false;
            break;
        case firmwareupdate::Status::CheckFailed:
            detail = tr("Couldn't check for updates.");
            break;
        case firmwareupdate::Status::Available:
        case firmwareupdate::Status::InstallFailed:
            status = tr("Update available");
            detail = snapshot.status == firmwareupdate::Status::InstallFailed
                ? tr("The update couldn't be installed.")
                : i18n::format(tr("Version {}, built on {}"), { snapshot.release.version, formatDate(snapshot.release.created) });
            if (canInstall(snapshot)) {
                action = tr("Update");
            } else if (snapshot.installSupported) {
                // Only a full install writes the partition table and sizes that the release needs
                detail += "\n";
                detail += tr("This update needs a full install with the web flasher.");
            } else {
                detail += "\n";
                detail += tr("Install this update with the web flasher.");
            }
            break;
        case firmwareupdate::Status::Installing:
            status = tr("Update available");
            action = tr("Updating...");
            actionEnabled = false;
            detail = snapshot.downloading
                ? i18n::format(tr("Downloading update: {}%"), { std::to_string(snapshot.progress) })
                : std::string(tr("Preparing update..."));
            break;
        case firmwareupdate::Status::Installed:
            status = tr("Update installed");
            action = tr("Restart");
            detail = tr("Restart to use update");
            break;
    }
    lv_label_set_text(ctx->statusLabel, status);
    lv_label_set_text(ctx->detailLabel, detail.c_str());
    if (isForceUpdateShown(ctx)) {
        action = tr("Update");
    }
    lv_label_set_text(ctx->actionLabel, action);
    lv_obj_set_state(ctx->actionButton, LV_STATE_DISABLED, !actionEnabled);

    const bool installing = snapshot.status == firmwareupdate::Status::Installing;
    // Erasing the flash gives no progress, so a spinner shows that it runs
    lv_obj_set_hidden(ctx->progressSpinner, !installing || snapshot.downloading);
    lv_obj_set_hidden(ctx->progressBar, !installing || !snapshot.downloading);
    if (installing) {
        lv_bar_set_value(ctx->progressBar, snapshot.progress, LV_ANIM_OFF);
    }
    lv_label_set_text(ctx->channelLabel, getChannelName(snapshot.channel));
    lv_obj_set_state(ctx->channelButton, LV_STATE_DISABLED, installing);
}

/** Requires the LVGL lock */
void updateRepositoriesWidgets(Context* ctx) {
    if (ctx->repositoriesLabel == nullptr) {
        return;
    }
    const char* pattern = ctx->repositoryCount == 1 ? tr("{} repository") : tr("{} repositories");
    lv_label_set_text(ctx->repositoriesLabel, i18n::format(pattern, { std::to_string(ctx->repositoryCount) }).c_str());
}

/** A container with one button, so the keys can move to it */
lv_obj_t* createButtonGroup(lv_obj_t* row) {
    auto* group = createRow(row);
    lv_obj_set_width(group, LV_SIZE_CONTENT);
    lvgl_grid_navigation_add(group);
    return group;
}

void createFirmwareCard(lv_obj_t* parent, Context* ctx) {
    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, LV_DPX(8), LV_STATE_DEFAULT);

    auto* updateRow = createRow(card);
    auto* updateTexts = createTexts(updateRow);
    ctx->statusLabel = createLabel(updateTexts, false);
    lv_obj_set_clickable(ctx->statusLabel, true);
    lv_obj_add_event_cb(ctx->statusLabel, onStatusPressed, LV_EVENT_SHORT_CLICKED, ctx);
    ctx->detailLabel = createLabel(updateTexts, true);
    ctx->actionButton = createTextButton(createButtonGroup(updateRow), "", onActionPressed, ctx);
    ctx->actionLabel = lv_obj_get_child(ctx->actionButton, 0);

    ctx->progressBar = lv_bar_create(card);
    lv_obj_set_width(ctx->progressBar, LV_PCT(100));
    lv_bar_set_range(ctx->progressBar, 0, 100);
    ctx->progressSpinner = lvgl_spinner_create(card);

    auto* channelRow = createRow(card);
    auto* channelTexts = createTexts(channelRow);
    lv_label_set_text(createLabel(channelTexts, false), tr("Update channel"));
    ctx->channelLabel = createLabel(channelTexts, true);
    lv_obj_set_clickable(channelTexts, true);
    lv_obj_add_event_cb(channelTexts, onChannelLabelPressed, LV_EVENT_SHORT_CLICKED, ctx);
    ctx->channelButton = createTextButton(createButtonGroup(channelRow), LV_SYMBOL_LIST, onChannelPressed, ctx);

    updateFirmwareWidgets(ctx);
}

void createRepositoriesCard(lv_obj_t* parent, Context* ctx) {
    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    auto* row = createRow(card);
    auto* texts = createTexts(row);
    auto* title = createLabel(texts, false);
    lv_label_set_text(title, tr("App Hub Repositories"));
    ctx->repositoriesLabel = createLabel(texts, true);

    createTextButton(createButtonGroup(row), LV_SYMBOL_RIGHT, onRepositoriesPressed, ctx);

    updateRepositoriesWidgets(ctx);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Software"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_style_pad_row(wrapper, LV_DPX(8), LV_STATE_DEFAULT);

    createFirmwareCard(wrapper, ctx);
    createRepositoriesCard(wrapper, ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->statusLabel = nullptr;
    ctx->detailLabel = nullptr;
    ctx->progressBar = nullptr;
    ctx->progressSpinner = nullptr;
    ctx->actionButton = nullptr;
    ctx->actionLabel = nullptr;
    ctx->channelLabel = nullptr;
    ctx->channelButton = nullptr;
    ctx->repositoriesLabel = nullptr;
}

/** Reads storage, so it is called without the LVGL lock */
void loadRepositoryCount(Context* ctx) {
    size_t count = 0;
    app_hub_repository_for_each([](const AppHubRepository* /*repository*/, void* context) {
        (*static_cast<size_t*>(context))++;
        return true;
    }, &count);
    lvgl_lock();
    ctx->repositoryCount = count;
    updateRepositoriesWidgets(ctx);
    lvgl_unlock();
}

void loadSnapshot(Context* ctx) {
    const firmwareupdate::Snapshot snapshot = firmwareupdate::getSnapshot();
    lvgl_lock();
    ctx->snapshot = snapshot;
    updateFirmwareWidgets(ctx);
    lvgl_unlock();
}

void startInstallDialog(Context* ctx) {
    strcpy(ctx->confirmCommit, ctx->snapshot.release.gitCommit);
    const std::string message = i18n::format(
        tr("Download and install version {}? Keep the device powered on until the update is installed."),
        { ctx->snapshot.release.version }
    );
    dialog::alert(ctx->appInstanceId, tr("Install update"), message, { tr("Cancel"), tr("Install") }, [](int32_t button, void* userData) {
        auto* ctx = static_cast<Context*>(userData);
        ctx->dialogResult = button;
        task_event_group_signal(ctx->eventGroup, ctx->installDialogBit);
    }, ctx);
}

void startForceDialog(Context* ctx) {
    const std::string message = i18n::format(
        tr("Download and install the latest build of the {} channel, also when it isn't newer? Keep the device powered on until the update is installed."),
        { getChannelName(ctx->snapshot.channel) }
    );
    dialog::alert(ctx->appInstanceId, tr("Force update"), message, { tr("Cancel"), tr("Update") }, [](int32_t button, void* userData) {
        auto* ctx = static_cast<Context*>(userData);
        ctx->dialogResult = button;
        task_event_group_signal(ctx->eventGroup, ctx->forceDialogBit);
    }, ctx);
}

void startDevelopDialog(Context* ctx) {
    dialog::alert(
        ctx->appInstanceId,
        tr("Develop channel"),
        tr("Builds of the develop channel are made from pull requests that aren't finished or tested. They can be unstable or lose your data. Switch to the develop channel?"),
        { tr("Cancel"), tr("Switch") },
        [](int32_t button, void* userData) {
            auto* ctx = static_cast<Context*>(userData);
            ctx->dialogResult = button;
            task_event_group_signal(ctx->eventGroup, ctx->developDialogBit);
        },
        ctx
    );
}

void startRestartDialog(Context* ctx) {
    dialog::alert(ctx->appInstanceId, tr("Update installed"), tr("Restart now to use the new version?"), { tr("Later"), tr("Restart") }, [](int32_t button, void* userData) {
        auto* ctx = static_cast<Context*>(userData);
        ctx->dialogResult = button;
        task_event_group_signal(ctx->eventGroup, ctx->restartDialogBit);
    }, ctx);
}

/** The channels of the selection dialog, in order */
std::vector<firmwareupdate::Channel> getListedChannels(firmwareupdate::Channel current) {
    std::vector<firmwareupdate::Channel> channels;
    for (const auto channel : SELECTABLE_CHANNELS) {
        if (isChannelListed(channel, current)) {
            channels.push_back(channel);
        }
    }
    return channels;
}

void startChannelDialog(Context* ctx) {
    std::vector<std::string> items;
    for (const auto channel : getListedChannels(ctx->snapshot.channel)) {
        items.emplace_back(getChannelName(channel));
    }
    dialog::selection(ctx->appInstanceId, tr("Update channel"), items, [](int32_t index, void* userData) {
        auto* ctx = static_cast<Context*>(userData);
        ctx->dialogResult = index;
        task_event_group_signal(ctx->eventGroup, ctx->channelDialogBit);
    }, ctx);
}

void onChannelResult(Context* ctx) {
    const auto channels = getListedChannels(ctx->snapshot.channel);
    const int32_t index = ctx->dialogResult;
    if (index >= 0 && static_cast<size_t>(index) < channels.size()) {
        firmwareupdate::setChannel(channels[index]);
    }
}

/** An installed update runs after a restart, which is offered once while the app runs */
void offerRestart(Context* ctx) {
    if (ctx->snapshot.status != firmwareupdate::Status::Installed) {
        ctx->restartOffered = false;
    } else if (!ctx->restartOffered) {
        ctx->restartOffered = true;
        startRestartDialog(ctx);
    }
}

/** An update that doesn't match this device can only be installed with the web installer */
void explainIncompatible(Context* ctx) {
    const bool incompatible = ctx->snapshot.status == firmwareupdate::Status::InstallFailed &&
        ctx->snapshot.installError == firmwareupdate::InstallError::Incompatible;
    if (!incompatible) {
        ctx->incompatibleExplained = false;
    } else if (!ctx->incompatibleExplained) {
        ctx->incompatibleExplained = true;
        dialog::alert(
            ctx->appInstanceId,
            tr("Incompatible update"),
            tr("Install this update from install.tactilityproject.org")
        );
    }
}

void openRepositories(Context* ctx) {
    AppStartContext context;
    if (app_start_context_from_id(APP_HUB_REPOSITORIES_APP_ID, &context) != ERROR_NONE) {
        LOG_E(TAG, "%s is not registered", APP_HUB_REPOSITORIES_APP_ID);
        return;
    }
    // The result tells when it closes, so the number of repositories can be read again
    app_start_context_set_parent(&context, ctx->appInstanceId);
    AppInstanceId instance = 0;
    app_start_with_context(&context, &instance);
}

void handleRequest(Context* ctx) {
    switch (ctx->request.exchange(Request::None)) {
        case Request::None:
            break;
        case Request::Check:
            firmwareupdate::requestCheck(true);
            break;
        case Request::Install:
            startInstallDialog(ctx);
            break;
        case Request::OpenRepositories:
            openRepositories(ctx);
            break;
        case Request::ForceUpdate:
            startForceDialog(ctx);
            break;
        case Request::EnableDevelop:
            startDevelopDialog(ctx);
            break;
        case Request::Restart:
            startRestartDialog(ctx);
            break;
        case Request::ChooseChannel:
            startChannelDialog(ctx);
            break;
    }
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    ctx.eventGroup = &eventGroup;
    check(task_event_group_claim_bit(&eventGroup, &ctx.requestBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.installDialogBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.forceDialogBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.developDialogBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.restartDialogBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.channelDialogBit) == ERROR_NONE);
    const bool firmwareSubscribed = firmwareupdate::subscribe(&eventGroup, &ctx.firmwareBit) == ERROR_NONE;

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);

    ctx.snapshot = firmwareupdate::getSnapshot();
    loadRepositoryCount(&ctx);

    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);
    if (window == 0) {
        // Without a window the app has no way to close, so it ends here
        app_event_unsubscribe(&appEvents);
        if (firmwareSubscribed) {
            firmwareupdate::unsubscribe(&eventGroup, ctx.firmwareBit);
        }
        task_event_group_destruct(&eventGroup);
        return -1;
    }

    firmwareupdate::requestCheck(false);
    offerRestart(&ctx);
    explainIncompatible(&ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, portMAX_DELAY);

        if (flags & ctx.requestBit) {
            handleRequest(&ctx);
        }

        if (firmwareSubscribed && (flags & ctx.firmwareBit)) {
            loadSnapshot(&ctx);
        }

        if (flags & ctx.installDialogBit) {
            // The second button installs
            if (ctx.dialogResult == 1) {
                const error_t error = firmwareupdate::requestInstall(ctx.confirmCommit);
                if (error != ERROR_NONE) {
                    LOG_E(TAG, "Failed to start the install: %s", error_to_string(error));
                }
            }
        }

        if (flags & ctx.forceDialogBit) {
            lvgl_lock();
            ctx.forceUpdateUnlocked = false;
            updateFirmwareWidgets(&ctx);
            lvgl_unlock();
            // The second button updates
            if (ctx.dialogResult == 1) {
                const error_t error = firmwareupdate::requestForcedUpdate();
                if (error != ERROR_NONE) {
                    LOG_E(TAG, "Failed to start the forced update: %s", error_to_string(error));
                }
            }
        }

        if (flags & ctx.developDialogBit) {
            // The second button switches
            if (ctx.dialogResult == 1) {
                firmwareupdate::setChannel(firmwareupdate::Channel::Develop);
            }
        }

        if (flags & ctx.channelDialogBit) {
            onChannelResult(&ctx);
        }

        if (flags & ctx.restartDialogBit) {
            // The second button restarts
            if (ctx.dialogResult == 1) {
#ifdef ESP_PLATFORM
                esp_restart();
#endif
            }
        }

        offerRestart(&ctx);
        explainIncompatible(&ctx);

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
            if (event.type == APP_EVENT_RESULT) {
                // The repositories app has ended
                app_manager_stop(event.result.launch_id);
                loadRepositoryCount(&ctx);
            }
        }
    }

    // Removing the window waits for its widgets, so no widget can signal the destroyed event group
    window_manager_remove(window);
    check(app_event_unsubscribe(&appEvents) == ERROR_NONE);
    if (firmwareSubscribed) {
        firmwareupdate::unsubscribe(&eventGroup, ctx.firmwareBit);
    }
    task_event_group_destruct(&eventGroup);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.softwaresettings",
    .name = "Software",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::softwaresettings
