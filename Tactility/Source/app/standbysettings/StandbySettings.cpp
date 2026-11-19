// SPDX-License-Identifier: Apache-2.0
#define L10N_APP "standbysettings"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/settings/RadioSettings.h>
#include <Tactility/settings/StandbySettings.h>
#include <Tactility/standby/Standby.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <mesh/mesh.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/device.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/wifi.h>

#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <string>
#include <vector>

namespace tt::app::standbysettings {

extern const ::AppManifest manifest;

namespace {

using settings::standby::StandbySettings;
using settings::standby::WakeKind;

constexpr uintptr_t ROW_DISPLAY = 0;
constexpr uintptr_t ROW_WIFI = 1;
constexpr uintptr_t ROW_BLUETOOTH = 2;
constexpr uintptr_t ROW_MESH = 3;
/** The wake source rows start here, with the index of the source added */
constexpr uintptr_t ROW_WAKE_FIRST = 4;

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t saveBit = 0;
    uint32_t standbyBit = 0;
    /** Guarded by the LVGL lock, as the checkboxes change it */
    StandbySettings settings;
    ::std::vector<standby::WakeSource> sources;
    bool hasDisplay = false;
    bool hasWifi = false;
    bool hasBluetooth = false;
    bool hasMesh = false;
    lv_obj_t* standbyButton = nullptr;
};

const char* getKindName(WakeKind kind) {
    switch (kind) {
        case WakeKind::Keyboard: return tr("Keyboard");
        case WakeKind::Touch: return tr("Touch");
        case WakeKind::Encoder: return tr("Encoder");
        case WakeKind::Button: return tr("Button");
    }
    return "";
}

bool anyWakeEnabled(const Context* ctx) {
    for (const auto& source : ctx->sources) {
        if (settings::standby::isWakeEnabled(ctx->settings, source.name, source.kind)) {
            return true;
        }
    }
    return false;
}

void updateStandbyButton(Context* ctx) {
    if (ctx->standbyButton != nullptr) {
        lv_obj_set_state(ctx->standbyButton, LV_STATE_DISABLED, !anyWakeEnabled(ctx));
    }
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onStandbyPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (anyWakeEnabled(ctx)) {
        task_event_group_signal(ctx->eventGroup, ctx->standbyBit);
    }
}

void onCheckboxChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* checkbox = lv_event_get_target_obj(event);
    const auto row = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(checkbox));
    const bool checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    if (row == ROW_DISPLAY) {
        ctx->settings.keepDisplay = checked;
    } else if (row == ROW_WIFI) {
        ctx->settings.keepWifi = checked;
    } else if (row == ROW_BLUETOOTH) {
        ctx->settings.keepBluetooth = checked;
    } else if (row == ROW_MESH) {
        ctx->settings.keepMesh = checked;
    } else if (row - ROW_WAKE_FIRST < ctx->sources.size()) {
        ctx->settings.wake[ctx->sources[row - ROW_WAKE_FIRST].name] = checked;
    }
    updateStandbyButton(ctx);
    // Saving is a file operation, which the app's task does
    task_event_group_signal(ctx->eventGroup, ctx->saveBit);
}

/** A label with a checkbox on its right */
void createRow(lv_obj_t* card, Context* ctx, const char* text, uintptr_t row, bool checked) {
    auto* container = lv_obj_create(card);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(container, false);

    auto* label = lv_label_create(container);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(label, text);

    auto* checkbox = lv_checkbox_create(container);
    lv_checkbox_set_text_static(checkbox, "");
    lv_obj_set_user_data(checkbox, reinterpret_cast<void*>(row));
    lv_obj_set_state(checkbox, LV_STATE_CHECKED, checked);
    lv_obj_add_event_cb(checkbox, onCheckboxChanged, LV_EVENT_VALUE_CHANGED, ctx);
    // The arrow keys would toggle a checkbox that is in the input group itself
    lvgl_grid_navigation_add(container);
}

lv_obj_t* createSection(lv_obj_t* parent, const char* title) {
    auto* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, title);

    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, LV_STATE_DEFAULT);
    return card;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Standby"));
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_style_pad_row(wrapper, 8, LV_STATE_DEFAULT);

    if (ctx->hasDisplay || ctx->hasWifi || ctx->hasBluetooth || ctx->hasMesh) {
        auto* card = createSection(wrapper, tr("Keep awake"));
        if (ctx->hasDisplay) {
            createRow(card, ctx, tr("Display"), ROW_DISPLAY, ctx->settings.keepDisplay);
        }
        if (ctx->hasWifi) {
            createRow(card, ctx, tr("Wi-Fi"), ROW_WIFI, ctx->settings.keepWifi);
        }
        if (ctx->hasBluetooth) {
            createRow(card, ctx, tr("Bluetooth"), ROW_BLUETOOTH, ctx->settings.keepBluetooth);
        }
        if (ctx->hasMesh) {
            createRow(card, ctx, tr("Mesh"), ROW_MESH, ctx->settings.keepMesh);
        }
        lvgl_list_hide_last_divider(card);
    }

    if (!ctx->sources.empty()) {
        auto* card = createSection(wrapper, tr("Wake on"));
        for (size_t i = 0; i < ctx->sources.size(); i++) {
            const auto& source = ctx->sources[i];
            const ::std::string text = ::std::string(getKindName(source.kind)) + " (" + source.name + ")";
            createRow(card, ctx, text.c_str(), ROW_WAKE_FIRST + i, settings::standby::isWakeEnabled(ctx->settings, source.name, source.kind));
        }
        lvgl_list_hide_last_divider(card);
    }

    auto* button = lv_button_create(wrapper);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, tr("Standby now"));
    lv_obj_add_event_cb(button, onStandbyPressed, LV_EVENT_SHORT_CLICKED, ctx);
    lvgl_grid_navigation_add(wrapper);
    ctx->standbyButton = button;
    updateStandbyButton(ctx);
}

void destroyWidgets(void* userData) {
    static_cast<Context*>(userData)->standbyButton = nullptr;
}

/** Mesh can be toggled once it was configured: a backend is selected and the region is set */
bool isMeshConfigured() {
    char backend[MESH_BACKEND_ID_MAX_LENGTH + 1];
    return mesh_is_available() &&
        mesh_get_backend(backend, sizeof(backend)) == ERROR_NONE &&
        settings::radio::loadRegion() != RADIO_REGION_UNSET;
}

StandbySettings copySettings(Context* ctx) {
    lvgl_lock();
    StandbySettings copy = ctx->settings;
    lvgl_unlock();
    return copy;
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();
    ctx.settings = settings::standby::loadOrGetDefault();
    ctx.sources = standby::findWakeSources();
    ctx.hasDisplay = device_exists_of_type(&DISPLAY_TYPE);
    ctx.hasWifi = device_exists_of_type(&WIFI_TYPE);
    ctx.hasBluetooth = device_exists_of_type(&BLUETOOTH_TYPE);
    ctx.hasMesh = isMeshConfigured();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    ctx.eventGroup = &eventGroup;
    check(task_event_group_claim_bit(&eventGroup, &ctx.saveBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.standbyBit) == ERROR_NONE);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);

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

        if (flags & (ctx.saveBit | ctx.standbyBit)) {
            settings::standby::save(copySettings(&ctx));
        }
        if (flags & ctx.standbyBit) {
            standby::enter(copySettings(&ctx));
        }

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
    }

    // Removing the window waits for its widgets, so no checkbox can signal the destroyed event group
    window_manager_remove(window);
    check(app_event_unsubscribe(&appEvents) == ERROR_NONE);
    task_event_group_destruct(&eventGroup);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.standbysettings",
    .name = "Standby",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::standbysettings
