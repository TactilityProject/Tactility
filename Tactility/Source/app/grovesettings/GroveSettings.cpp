#include <string>
#include <vector>

#include <lvgl.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/grove.h>
#include <tactility/log.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/toolbar.h>

#include <Tactility/settings/GrovePortSettings.h>

namespace tt::app::grovesettings {

extern const ::AppManifest manifest;

namespace {

constexpr auto* TAG = "GroveSettings";

// How long the app task waits before it saves modes again after a failed save
constexpr TickType_t SAVE_RETRY_TICKS = pdMS_TO_TICKS(5000);

struct PendingMode {
    std::string deviceName;
    GroveMode mode;
};

struct Context {
    uint32_t appInstanceId;
    // Used by the widgets only
    std::vector<::Device*> devices;
    TaskEventGroup* eventGroup;
    uint32_t saveBit;
    // The modes selected and not saved yet. Guarded by the LVGL lock.
    std::vector<PendingMode> pendingModes;
};

/** Must be called with the LVGL lock held. Keeps the newest mode per device. */
void addPendingMode(Context* ctx, const char* deviceName, GroveMode mode) {
    for (auto& entry : ctx->pendingModes) {
        if (entry.deviceName == deviceName) {
            entry.mode = mode;
            return;
        }
    }
    ctx->pendingModes.push_back({ deviceName, mode });
}


void collectDevices(Context* ctx) {
    ctx->devices.clear();
    device_for_each_of_type(&GROVE_TYPE, &ctx->devices, [](auto* device, auto* context) {
        auto* vec = static_cast<std::vector<::Device*>*>(context);
        vec->push_back(device);
        return true;
    });
}

struct ModeOption {
    GroveMode mode;
    const char* name;
};

constexpr ModeOption MODE_OPTIONS[] = {
    { GROVE_MODE_DISABLED, "Disabled" },
    { GROVE_MODE_I2C, "I2C" },
    { GROVE_MODE_UART, "UART" },
};

GroveMode getChipMode(lv_obj_t* chip) {
    return static_cast<GroveMode>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(chip)));
}

void updateChips(lv_obj_t* chipsRow, GroveMode mode) {
    const uint32_t count = lv_obj_get_child_count(chipsRow);
    for (uint32_t i = 0; i < count; i++) {
        auto* chip = lv_obj_get_child(chipsRow, static_cast<int32_t>(i));
        lv_obj_set_state(chip, LV_STATE_CHECKED, getChipMode(chip) == mode);
    }
}

// The modes are saved by the app task, as file operations must not run on the LVGL thread
void onModeChipPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    auto* chip = lv_event_get_target_obj(e);
    auto* chipsRow = lv_obj_get_parent(chip);
    auto* device = static_cast<::Device*>(lv_obj_get_user_data(chipsRow));
    const GroveMode mode = getChipMode(chip);
    if (grove_set_mode(device, mode) == ERROR_NONE) {
        updateChips(chipsRow, mode);
        addPendingMode(ctx, device->name, mode);
        task_event_group_signal(ctx->eventGroup, ctx->saveBit);
    }
}

/**
 * Saves the selected modes. Saves that fail stay pending, unless a newer mode was selected meanwhile.
 * @return false when a save failed
 */
bool savePendingModes(Context* ctx) {
    lvgl_lock();
    std::vector<PendingMode> pending;
    pending.swap(ctx->pendingModes);
    lvgl_unlock();

    std::vector<PendingMode> failed;
    for (const auto& entry : pending) {
        if (!settings::grove::saveMode(entry.deviceName.c_str(), entry.mode)) {
            failed.push_back(entry);
        }
    }
    if (failed.empty()) {
        return true;
    }

    lvgl_lock();
    for (const auto& entry : failed) {
        bool newer = false;
        for (const auto& current : ctx->pendingModes) {
            newer = newer || current.deviceName == entry.deviceName;
        }
        if (!newer) {
            ctx->pendingModes.push_back(entry);
        }
    }
    lvgl_unlock();
    return false;
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    collectDevices(ctx);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Grove");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    for (auto* device : ctx->devices) {
        auto* label = lv_label_create(main_wrapper);
        lv_label_set_text(label, device->name);

        // The chips are centered in the card while they fit, and scroll when they don't
        auto* card = lvgl_card_create(main_wrapper);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(label, lv_obj_get_style_pad_left(card, LV_PART_MAIN), LV_STATE_DEFAULT);

        auto* chips_row = lv_obj_create(card);
        lv_obj_set_size(chips_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(chips_row, LV_PCT(100), LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(chips_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_scroll_dir(chips_row, LV_DIR_HOR);
        lv_obj_set_style_bg_opa(chips_row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(chips_row, 0, LV_STATE_DEFAULT);
        // The chips' margins leave room for their focus rings and space them apart
        lv_obj_set_style_pad_all(chips_row, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_column(chips_row, 0, LV_STATE_DEFAULT);
        lv_obj_set_user_data(chips_row, device);

        for (const auto& option : MODE_OPTIONS) {
            auto* chip = lvgl_chip_create(chips_row);
            lv_label_set_text(lv_label_create(chip), option.name);
            lv_obj_set_user_data(chip, reinterpret_cast<void*>(static_cast<intptr_t>(option.mode)));
            lv_obj_add_event_cb(chip, onModeChipPressed, LV_EVENT_SHORT_CLICKED, ctx);
        }
        lvgl_grid_navigation_add(chips_row);

        GroveMode current = GROVE_MODE_DISABLED;
        grove_get_mode(device, &current);
        updateChips(chips_row, current);
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.saveBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    bool saveFailed = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, saveFailed ? SAVE_RETRY_TICKS : portMAX_DELAY);

        saveFailed = !savePendingModes(&ctx);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    if (!savePendingModes(&ctx)) {
        LOG_E(TAG, "Failed to save the Grove modes, they're lost at restart");
    }
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_release_bit(&event_group, ctx.saveBit);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.grovesettings",
    .name = "Grove",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

}
