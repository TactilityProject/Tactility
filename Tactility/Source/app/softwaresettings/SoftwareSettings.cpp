// SPDX-License-Identifier: Apache-2.0
#define L10N_APP "softwaresettings"
#include <Tactility/i18n/AppDomain.h>

#include <Tactility/app/dialog/Dialog.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <app_hub/app_hub.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <atomic>
#include <cstring>

namespace tt::app::softwaresettings {

extern const ::AppManifest manifest;

namespace {

constexpr auto* TAG = "SoftwareSettings";

enum class Request : int32_t {
    None = 0,
    SetEnabled,
    Remove,
    Add,
};

struct RepositoryItem {
    ::std::string url;
    bool enabled;
    bool official;
};

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t requestBit = 0;
    /** What the widgets show. Filled by the app's task, with the LVGL lock held while the widgets use it. */
    ::std::vector<RepositoryItem> repositories;
    /** Set by the widgets, handled by the app's task because the repositories are stored in files */
    ::std::atomic<Request> request { Request::None };
    ::std::atomic<size_t> requestIndex { 0 };
    ::std::atomic<bool> requestEnabled { false };
    /** Signaled by the address dialog, which stores its result in addressConfirmed and addressText first */
    uint32_t addressBit = 0;
    bool addressConfirmed = false;
    ::std::string addressText;
    /** nullptr while the widgets don't exist */
    lv_obj_t* list = nullptr;
};

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void signal(Context* ctx, Request request, size_t index, bool enabled) {
    ctx->requestIndex = index;
    ctx->requestEnabled = enabled;
    ctx->request = request;
    task_event_group_signal(ctx->eventGroup, ctx->requestBit);
}

void onSwitchChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sw = lv_event_get_target_obj(event);
    const auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(sw)));
    signal(ctx, Request::SetEnabled, index, lv_obj_has_state(sw, LV_STATE_CHECKED));
}

void onRemovePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    const auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    signal(ctx, Request::Remove, index, false);
}

void onAddPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    signal(ctx, Request::Add, 0, false);
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

/** @return "Tactility" for the official repository, otherwise the host name of the address */
::std::string getName(const RepositoryItem& repository) {
    if (repository.official) {
        return "Tactility";
    }
    constexpr size_t SCHEME_LENGTH = 8;
    const size_t end = repository.url.find('/', SCHEME_LENGTH);
    return repository.url.substr(SCHEME_LENGTH, end == std::string::npos ? std::string::npos : end - SCHEME_LENGTH);
}

/** Requires the LVGL lock. Fills the card with a row per repository. */
void populateList(Context* ctx) {
    lv_obj_clean(ctx->list);
    for (size_t i = 0; i < ctx->repositories.size(); i++) {
        const auto& repository = ctx->repositories[i];
        auto* row = createRow(ctx->list);

        auto* texts = lv_obj_create(row);
        lv_obj_remove_style_all(texts);
        lv_obj_set_height(texts, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(texts, 1);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(texts, 0, LV_STATE_DEFAULT);
        lv_obj_set_scrollable(texts, false);

        // The name is the official name, or else the host of the address, with the full address below it
        auto* name = lv_label_create(texts);
        lv_obj_set_width(name, LV_PCT(100));
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(name, getName(repository).c_str());

        auto* url = lv_label_create(texts);
        lv_obj_set_width(url, LV_PCT(100));
        lv_obj_set_style_text_font(url, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
        lv_label_set_long_mode(url, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(url, repository.url.c_str());

        if (!repository.official) {
            auto* remove = lv_button_create(row);
            lv_obj_set_user_data(remove, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
            lv_label_set_text(lv_label_create(remove), LV_SYMBOL_TRASH);
            lv_obj_add_event_cb(remove, onRemovePressed, LV_EVENT_SHORT_CLICKED, ctx);
        }

        auto* sw = lv_switch_create(row);
        lv_obj_set_user_data(sw, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_set_state(sw, LV_STATE_CHECKED, repository.enabled);
        lv_obj_add_event_cb(sw, onSwitchChanged, LV_EVENT_VALUE_CHANGED, ctx);
        // The arrow keys would toggle a switch that is in the input group itself
        lvgl_grid_navigation_add(row);
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Software"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    lvgl_toolbar_add_text_button_action(toolbar, LV_SYMBOL_PLUS, onAddPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_style_pad_row(wrapper, 8, LV_STATE_DEFAULT);

    auto* title = lv_label_create(wrapper);
    lv_label_set_text(title, tr("App repositories"));

    ctx->list = lvgl_card_create(wrapper);
    lv_obj_set_size(ctx->list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->list, 8, LV_STATE_DEFAULT);
    populateList(ctx);

}

void destroyWidgets(void* userData) {
    static_cast<Context*>(userData)->list = nullptr;
}

/** Reads the repositories. It reads storage, so it is called without the LVGL lock. */
void loadRepositories(Context* ctx) {
    ::std::vector<RepositoryItem> repositories;
    app_hub_repository_for_each([](const AppHubRepository* repository, void* context) {
        static_cast<::std::vector<RepositoryItem>*>(context)->push_back({ repository->url, repository->enabled, repository->official });
        return true;
    }, &repositories);
    lvgl_lock();
    ctx->repositories = ::std::move(repositories);
    if (ctx->list != nullptr) {
        populateList(ctx);
    }
    lvgl_unlock();
}

void showError(Context* ctx, error_t error) {
    const char* message;
    switch (error) {
        case ERROR_INVALID_ARGUMENT:
            message = tr("The address must start with https://");
            break;
        case ERROR_ALREADY_EXISTS:
            message = tr("This repository is in the list already.");
            break;
        case ERROR_OUT_OF_RANGE:
            message = tr("There is no room for more repositories.");
            break;
        default:
            message = tr("The change could not be saved.");
            break;
    }
    dialog::alert(ctx->appInstanceId, tr("Error"), message);
}

void startAddressInput(Context* ctx);

/** Warns about the risk first, as an app from a repository can do anything on the device */
void startWarning(Context* ctx) {
    dialog::alert(
        ctx->appInstanceId,
        tr("Third party apps"),
        tr("Apps from a third party repository are not checked by the Tactility project. They can contain harmful software that steals your data or damages your device. Only add a repository that you trust."),
        { tr("Cancel"), tr("Continue") },
        [](int32_t button, void* userData) {
            // The second button continues
            if (button == 1) {
                startAddressInput(static_cast<Context*>(userData));
            }
        },
        ctx
    );
}

void startAddressInput(Context* ctx) {
    dialog::input(
        ctx->appInstanceId,
        tr("Add repository"),
        tr("The address of the repository, starting with https://"),
        "https://",
        [](bool confirmed, const char* text, void* userData) {
            auto* ctx = static_cast<Context*>(userData);
            ctx->addressConfirmed = confirmed;
            ctx->addressText = text;
            task_event_group_signal(ctx->eventGroup, ctx->addressBit);
        },
        ctx
    );
}

void onAddressResult(Context* ctx) {
    if (ctx->addressConfirmed) {
        const error_t error = app_hub_repository_add(ctx->addressText.c_str());
        if (error != ERROR_NONE) {
            showError(ctx, error);
        }
    }
}

void handleRequest(Context* ctx) {
    const Request request = ctx->request.exchange(Request::None);
    const size_t index = ctx->requestIndex;
    switch (request) {
        case Request::None:
            return;
        case Request::Add:
            startWarning(ctx);
            return;
        case Request::SetEnabled:
        case Request::Remove: {
            if (index >= ctx->repositories.size()) {
                return;
            }
            const ::std::string url = ctx->repositories[index].url;
            const error_t error = request == Request::Remove ? app_hub_repository_remove(url.c_str()) : app_hub_repository_set_enabled(url.c_str(), ctx->requestEnabled);
            if (error != ERROR_NONE) {
                LOG_E(TAG, "Failed to change %s: %s", url.c_str(), error_to_string(error));
            }
            break;
        }
    }
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    ctx.eventGroup = &eventGroup;
    check(task_event_group_claim_bit(&eventGroup, &ctx.requestBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.addressBit) == ERROR_NONE);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);

    loadRepositories(&ctx);

    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);
    if (window == 0) {
        // Without a window the app has no way to close, so it ends here
        app_event_unsubscribe(&appEvents);
        task_event_group_destruct(&eventGroup);
        return -1;
    }

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&eventGroup, &flags, portMAX_DELAY);

        bool changed = false;
        if (flags & ctx.requestBit) {
            handleRequest(&ctx);
            changed = true;
        }

        if (flags & ctx.addressBit) {
            onAddressResult(&ctx);
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
            loadRepositories(&ctx);
        }
    }

    // Removing the window waits for its widgets, so no widget can signal the destroyed event group
    window_manager_remove(window);
    check(app_event_unsubscribe(&appEvents) == ERROR_NONE);
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
