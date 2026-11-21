#define L10N_APP "apphubdetails"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/TileGrid.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/apphub/AppHubIcon.h>
#include <Tactility/app/apphubdetails/AppHubDetailsApp.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <app_hub/app_hub.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/focus_container.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/spinner.h>
#include <lvgl/widgets/text_button.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <atomic>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>
#include <localisation/tr.h>

namespace tt::app::apphubdetails {

constexpr auto* TAG = "AppHubDetails";

extern const ::AppManifest manifest;

namespace {

constexpr size_t DESCRIPTION_MAX_SIZE = 2048;
/** At this theme size (0 is compact, 100 is regular) or lower, the icon doesn't show */
constexpr uint8_t COMPACT_SIZE_MAX = 20;

enum class Action {
    None,
    Install,
    Uninstall,
    Update
};

struct Context {
    uint32_t appInstanceId;
    tt::std::string appId;

    /** Null while the window is covered by another one */
    lv_obj_t* header = nullptr;
        /** The name, version and status. The action widgets follow it in the header. */
    lv_obj_t* texts = nullptr;
    lv_obj_t* nameLabel = nullptr;
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* iconImage = nullptr;
    lv_obj_t* incompatibleLabel = nullptr;
    lv_obj_t* descriptionLabel = nullptr;
    lv_obj_t* versionLabel = nullptr;
    /** Whether createWidgets() ran before, so the next call is a resurface */
    bool created = false;

    // Read from the catalog file on this app's own thread (see loadState()), as the widget code
    // can't do file operations. Written with the LVGL lock held, read by the widget code.
    AppHubApp app {};
    tt::std::string description = tr("Loading...");
    /** Whether this device can install and run the app, known once the details are loaded */
    bool compatible = false;
    /** Whether the details were read, so that `compatible` is known. Installing needs it. */
    bool detailsKnown = false;
    /** The catalog's version name, empty until the details are loaded */
    tt::std::string versionName;
    /** Whether the details were downloaded. Only used on this app's own thread. */
    bool detailsLoaded = false;
    /** The icon, null when it's not loaded or the app has none. Only set while the LVGL lock is held. */
    lv_draw_buf_t* icon = nullptr;
    bool iconRecolor = false;
    bool iconLoaded = false;
    /** The version of the app that the icon is of */
    uint64_t iconVersion = 0;
    /** Shows when the app has no icon, null when icons aren't shown */
    lv_draw_buf_t* defaultIcon = nullptr;

    /** Set by an action button on the LVGL task, run on this app's own thread */
    ::std::atomic<Action> requestedAction { Action::None };

    TaskEventGroup* eventGroup = nullptr;
    uint32_t reloadBit = 0;
    /** Set when the window resurfaces: the catalog can change while a dialog covers it */
    ::std::atomic<bool> reloadRequested { false };
    AppHubEventSubscription appHubSub {};
    /** App events cancel the downloads, so the app closes without waiting for one */
    AppHubInterrupt interrupt {};
};

AppHubApp readApp(Context* ctx) {
    AppHubApp app {};
    if (app_hub_get_app(ctx->appId.c_str(), &app) != ERROR_NONE) {
        strncpy(app.id, ctx->appId.c_str(), sizeof(app.id) - 1);
        strncpy(app.name, ctx->appId.c_str(), sizeof(app.name) - 1);
    }
    return app;
}

/** Downloads the details. Runs on this app's own thread. */
error_t readDetails(Context* ctx, tt::std::string& description, AppHubAppDetails& details) {
    tt::std::vector<char> buffer(DESCRIPTION_MAX_SIZE);
    const error_t error = app_hub_get_details(ctx->appId.c_str(), buffer.data(), buffer.size(), &details, &ctx->interrupt);
    if (error != ERROR_NONE) {
        return error;
    }
    if (buffer[0] == '\0') {
        description = tr("This app has no description yet.");
        return ERROR_NONE;
    }
    description = buffer.data();
    for (size_t pos = 0; (pos = description.find("\\n", pos)) != tt::std::string::npos;) {
        description.replace(pos, 2, "\n");
    }
    return ERROR_NONE;
}

/** The icon only shows with enough room and memory */
bool isIconShown() {
    return areSvgIconsEnabled() && lvgl_theme_get_size() > COMPACT_SIZE_MAX;
}

uint32_t getIconSize() {
    return lvgl_get_shared_icon_large_font_height() * 3 / 2;
}

/**
 * Reads the state from App Hub. Doesn't touch widgets, so it runs without the LVGL lock.
 * The details and the icon are downloaded when they weren't yet.
 * @return false when an app event interrupted a download, so the state is incomplete
 */
bool loadState(Context* ctx) {
    const auto app = readApp(ctx);
    tt::std::string description;
    AppHubAppDetails details {};
    details.compatible = true;
    const bool loadDetails = !ctx->detailsLoaded;
    bool detailsRead = false;
    if (loadDetails) {
        if (app.in_catalog) {
            const error_t error = readDetails(ctx, description, details);
            if (error == ERROR_INVALID_STATE) {
                return false;
            }
            detailsRead = error == ERROR_NONE;
            if (!detailsRead) {
                description = tr("Failed to load the details");
            }
        } else {
            description = tr("This app has no description yet.");
            detailsRead = true;
        }
    }

    lv_draw_buf_t* icon = nullptr;
    bool iconRecolor = false;
    bool iconInterrupted = false;
    // A new version of the app has a new icon
    bool loadIconNow = app.in_catalog && app.has_icon && (!ctx->iconLoaded || ctx->iconVersion != app.version_code) && isIconShown();
    if (loadIconNow) {
        icon = apphub::loadIcon(ctx->appId.c_str(), getIconSize(), &iconRecolor, &ctx->interrupt, &iconInterrupted);
        loadIconNow = !iconInterrupted;
    }

    lvgl_lock();
    ctx->app = app;
    if (loadDetails) {
        ctx->description = ::std::move(description);
        if (detailsRead) {
            ctx->compatible = details.compatible;
            ctx->versionName = details.version_name;
            ctx->detailsKnown = true;
        }
    }
    if (loadIconNow) {
        // The widget shows the new image before the old one is freed
        auto* oldIcon = ctx->icon;
        ctx->icon = icon;
        ctx->iconRecolor = iconRecolor;
        if (ctx->iconImage != nullptr) {
            if (icon != nullptr) {
                apphub::setIcon(ctx->iconImage, icon, iconRecolor);
            } else if (ctx->defaultIcon != nullptr) {
                apphub::setIcon(ctx->iconImage, ctx->defaultIcon, true);
            } else {
                // Nothing to show: the widget must not keep pointing at the image that is freed below
                lv_image_set_src(ctx->iconImage, nullptr);
            }
        }
        if (oldIcon != nullptr) {
            lv_draw_buf_destroy(oldIcon);
        }
    }
    lvgl_unlock();
    ctx->detailsLoaded = ctx->detailsLoaded || detailsRead;
    if (loadIconNow) {
        ctx->iconLoaded = true;
        ctx->iconVersion = app.version_code;
    }
    return !iconInterrupted;
}

void updateViews(Context* ctx);

/** Loads the state and shows it. Runs on this app's own thread. */
void refreshViews(Context* ctx) {
    if (!loadState(ctx)) {
        // Loaded again after the app's event loop handled the event, unless it closes
        ctx->reloadRequested = true;
    }
    lvgl_lock();
    updateViews(ctx);
    lvgl_unlock();
}

void onBackPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    app_event_emit_close(ctx->appInstanceId);
}

/** Queuing the operation reads the catalog, so it runs on this app's own thread */
void requestAction(lv_event_t* e, Action action) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->requestedAction = action;
    task_event_group_signal(ctx->eventGroup, ctx->reloadBit);
}

void onInstallPressed(lv_event_t* e) {
    requestAction(e, Action::Install);
}

void onUninstallPressed(lv_event_t* e) {
    requestAction(e, Action::Uninstall);
}

void onUpdatePressed(lv_event_t* e) {
    requestAction(e, Action::Update);
}

void queueOperation(Context* ctx, error_t (*operation)(const char* id, uint32_t* operation_id), const char* failure) {
    const error_t error = operation(ctx->appId.c_str(), nullptr);
    if (error == ERROR_RESOURCE_BUSY) {
        alertdialog::start(ctx->appInstanceId, tr("Busy"), tr("Another operation is pending. Try again when it's done."));
        return;
    } else if (error != ERROR_NONE) {
        alertdialog::start(ctx->appInstanceId, tr("Error"), failure);
        return;
    }
    refreshViews(ctx);
}

void runAction(Context* ctx, Action action) {
    switch (action) {
        case Action::None:
            break;
        case Action::Install:
            queueOperation(ctx, app_hub_install, tr("Failed to install app"));
            break;
        case Action::Uninstall:
            queueOperation(ctx, app_hub_remove, tr("Failed to uninstall app"));
            break;
        case Action::Update:
            queueOperation(ctx, app_hub_install, tr("Failed to update app"));
            break;
    }
}

void onOperationEvent(Context* ctx, const AppHubEvent& event) {
    const bool finished = event.type == APP_HUB_EVENT_INSTALLED || event.type == APP_HUB_EVENT_REMOVED;
    if (finished && ctx->appId == event.app_id && event.result != ERROR_NONE) {
        LOG_E(TAG, "Operation failed: %s", error_to_string(event.result));
        const char* message = (event.type == APP_HUB_EVENT_REMOVED) ? tr("Failed to uninstall app") : tr("Failed to install app");
        alertdialog::start(ctx->appInstanceId, tr("Error"), message);
    }
    refreshViews(ctx);
}

void addActionButtons(Context* ctx) {
    const auto& app = ctx->app;
    if (app.installed) {
        if (app.update_available && ctx->detailsKnown && ctx->compatible) {
            auto* update = lvgl_icon_button_create_variant(ctx->header, LVGL_ICON_BUTTON_FILLED);
            lv_obj_add_event_cb(update, onUpdatePressed, LV_EVENT_SHORT_CLICKED, ctx);
            lv_label_set_text(lv_label_create(update), tr("Update"));
        }
        auto* uninstall = lvgl_text_button_create(ctx->header);
        lv_obj_add_event_cb(uninstall, onUninstallPressed, LV_EVENT_SHORT_CLICKED, ctx);
        lv_label_set_text(lv_label_create(uninstall), tr("Uninstall"));
    } else if (app.in_catalog && ctx->detailsKnown && ctx->compatible) {
        auto* install = lvgl_icon_button_create_variant(ctx->header, LVGL_ICON_BUTTON_FILLED);
        lv_obj_add_event_cb(install, onInstallPressed, LV_EVENT_SHORT_CLICKED, ctx);
        lv_label_set_text(lv_label_create(install), tr("Install"));
    }
}

void updateViews(Context* ctx) {
    // Covered by another window (e.g. a dialog): createWidgets() updates the views when it's shown again
    if (ctx->header == nullptr) {
        return;
    }
    if (ctx->iconImage != nullptr) {
        if (ctx->icon != nullptr) {
            apphub::setIcon(ctx->iconImage, ctx->icon, ctx->iconRecolor);
        } else if (ctx->defaultIcon != nullptr) {
            apphub::setIcon(ctx->iconImage, ctx->defaultIcon, true);
        }
    }
    lv_obj_set_hidden(ctx->incompatibleLabel, !ctx->detailsKnown || ctx->compatible);
    lv_label_set_text(ctx->descriptionLabel, ctx->description.c_str());
    const auto& app = ctx->app;
    lv_label_set_text(ctx->nameLabel, app.name);
    const char* versionName = ctx->versionName.empty() ? app.installed_version_name : ctx->versionName.c_str();
    lv_label_set_text(ctx->versionLabel, versionName[0] == '\0' ? "" : i18n::format(tr("Version {}"), { versionName }).c_str());

    const bool updateAvailable = app.installed && app.update_available;
    lv_obj_set_hidden(ctx->statusLabel, !app.installed);
    lv_label_set_text(ctx->statusLabel, updateAvailable ? tr("Update available!") : tr("Installed"));
    lv_obj_set_style_text_color(ctx->statusLabel, updateAvailable ? lv_color_make(0xff, 0xff, 0x00) : lv_theme_get_color_secondary(ctx->statusLabel), LV_STATE_DEFAULT);

    // The actions are the header's widgets after the texts, so they're in the header's grid navigation group
    while (lv_obj_get_index(lv_obj_get_child(ctx->header, -1)) > lv_obj_get_index(ctx->texts)) {
        lv_obj_delete(lv_obj_get_child(ctx->header, -1));
    }
    // This app's install or removal continues after the window closes, so it can already be queued or running when the window opens.
    if (app.operation_pending) {
        lv_obj_set_clickable(lvgl_spinner_create(ctx->header), false);
    } else {
        addActionButtons(ctx);
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    // Replaces the toolbar: the close button, the app's icon, name and version, and the actions
    ctx->header = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->header);
    lv_obj_set_size(ctx->header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const auto spacing = lvgl_theme_lerp(LV_DPX(2), LV_DPX(8), lvgl_theme_get_spacing());
    lv_obj_set_style_pad_all(ctx->header, spacing, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ctx->header, spacing, LV_STATE_DEFAULT);

    auto* closeButton = lvgl_icon_button_create(ctx->header);
    lv_obj_add_event_cb(closeButton, onBackPressed, LV_EVENT_SHORT_CLICKED, ctx);
    auto* closeIcon = lv_label_create(closeButton);
    lv_obj_set_style_text_font(closeIcon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(closeIcon, LVGL_ICON_SHARED_CLOSE);

    if (isIconShown()) {
        ctx->iconImage = lv_image_create(ctx->header);
        lv_obj_set_size(ctx->iconImage, getIconSize(), getIconSize());
    }

    auto* texts = lv_obj_create(ctx->header);
    ctx->texts = texts;
    lv_obj_remove_style_all(texts);
    lv_obj_set_size(texts, 0, LV_SIZE_CONTENT);
    // Grid navigation focuses clickable children
    lv_obj_set_clickable(texts, false);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);

    ctx->nameLabel = lv_label_create(texts);
    lv_obj_set_style_text_font(ctx->nameLabel, lvgl_get_text_font(FONT_SIZE_LARGE), LV_STATE_DEFAULT);
    // The label needs a fixed height to end in an ellipsis, as it would wrap otherwise
    lv_label_set_long_mode(ctx->nameLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(ctx->nameLabel, LV_PCT(100), lv_font_get_line_height(lvgl_get_text_font(FONT_SIZE_LARGE)));

    ctx->versionLabel = lv_label_create(texts);
    lv_obj_set_style_text_font(ctx->versionLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    ctx->statusLabel = lv_label_create(texts);
    lv_obj_set_style_text_font(ctx->statusLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    // Focusable widgets are direct children of the header, so the close button and the actions are one grid navigation group
    lvgl_grid_navigation_add(ctx->header);

    // A focus container is focusable, so the keys can scroll the description, and it shows a focus ring
    auto* wrapper = lvgl_focus_container_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(wrapper, true);
    lv_obj_set_scroll_dir(wrapper, LV_DIR_VER);
    // Room for the focus ring, which the parent would clip
    const auto ringSpace = lvgl_theme_lerp(LV_DPX(3), LV_DPX(8), lvgl_theme_get_size());
    lv_obj_set_style_margin_all(wrapper, ringSpace, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(wrapper, lvgl_theme_lerp(LV_DPX(2), LV_DPX(8), lvgl_theme_get_spacing()), LV_STATE_DEFAULT);

    ctx->incompatibleLabel = lv_label_create(wrapper);
    lv_label_set_text(ctx->incompatibleLabel, tr("Not compatible with this device"));
    lv_obj_set_style_text_color(ctx->incompatibleLabel, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT);

    ctx->descriptionLabel = lv_label_create(wrapper);
    lv_obj_set_width(ctx->descriptionLabel, LV_PCT(100));
    lv_label_set_long_mode(ctx->descriptionLabel, LV_LABEL_LONG_MODE_WRAP);

    updateViews(ctx);

    // The cached state shows right away. The app's task then reloads it, as the catalog can have
    // changed while a dialog covered this window.
    if (ctx->created) {
        ctx->reloadRequested = true;
        task_event_group_signal(ctx->eventGroup, ctx->reloadBit);
    }
    ctx->created = true;
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->header = nullptr;
    ctx->texts = nullptr;
    ctx->nameLabel = nullptr;
    ctx->statusLabel = nullptr;
    ctx->iconImage = nullptr;
    ctx->incompatibleLabel = nullptr;
    ctx->descriptionLabel = nullptr;
    ctx->versionLabel = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    // argv layout: [0]=appId

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    if (argc >= 1) {
        ctx.appId = argv[0];
    }

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.reloadBit) == ERROR_NONE);
    check(app_hub_event_subscribe(&ctx.appHubSub, &event_group) == ERROR_NONE);
    ctx.interrupt = { &event_group, sub.bit };

    ctx.app = readApp(&ctx);
    if (isIconShown()) {
        ctx.defaultIcon = apphub::loadDefaultIcon(getIconSize());
    }
    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);
    // The details are downloaded after the window shows
    ctx.reloadRequested = true;
    task_event_group_signal(&event_group, ctx.reloadBit);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT:
                    // An error dialog was closed
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }

        AppHubEvent app_hub_event {};
        while (!shouldClose && app_hub_event_poll(&ctx.appHubSub, &app_hub_event) == ERROR_NONE) {
            if (app_hub_event.type == APP_HUB_EVENT_REFRESHED) {
                // A new catalog can change the version, description and actions
                ctx.detailsLoaded = false;
                ctx.reloadRequested = true;
            } else if (app_hub_event.type != APP_HUB_EVENT_REFRESHING) {
                onOperationEvent(&ctx, app_hub_event);
            }
        }

        if (!shouldClose) {
            runAction(&ctx, ctx.requestedAction.exchange(Action::None));
        }
        if (!shouldClose && ctx.reloadRequested.exchange(false)) {
            refreshViews(&ctx);
        }
    }

    // Queued operations continue in the background after the window closes
    window_manager_remove(window);
    if (ctx.icon != nullptr) {
        lv_draw_buf_destroy(ctx.icon);
    }
    if (ctx.defaultIcon != nullptr) {
        lv_draw_buf_destroy(ctx.defaultIcon);
    }
    check(app_hub_event_unsubscribe(&ctx.appHubSub) == ERROR_NONE);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

void start(const char* appId) {
    // Fire-and-forget (parent_instance_id 0): AppHub's own multi-app browsing list isn't
    // waiting on a result.
    const char* argv[] = { appId };
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_arguments_ext(&context, 1, argv);
    app_start_context_set_parent(&context, /*parent_instance_id=*/0);
    app_start_with_context(&context, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.apphubdetails",
    .name = "App Details",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
