#include "SimulatorProfile.h"

#include <app/event.h>
#include <app/location.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>

#include <atomic>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/text_button.h>
#include <lvgl/widgets/toolbar.h>

namespace simulator {

namespace {

constexpr auto* SELECTION_DIALOG_ID = "tactility.selectiondialog";
constexpr const char* ORIENTATION_LABELS[] = { "Default", "Landscape", "Portrait" };
constexpr int ORIENTATION_COUNT = 3;

struct Context {
    uint32_t appInstanceId = 0;
    TaskEventGroup eventGroup {};
    uint32_t selectProfileBit = 0;
    uint32_t rebootBit = 0;
    uint32_t orientationBit = 0;
    /** Set by the pills on the LVGL task, taken by the app's task. -1 when there is none. */
    std::atomic<int> requestedOrientation { -1 };
    AppInstanceId dialogId = 0;
    /** Last read from the profile file, which is not on the LVGL thread */
    size_t selectedIndex = 0;
    OrientationOverride selectedOrientation = OrientationOverride::Default;
    bool saveFailed = false;

    lv_obj_t* profileLabel = nullptr;
    lv_obj_t* rebootButton = nullptr;
    lv_obj_t* orientationChips = nullptr;
    lv_obj_t* errorLabel = nullptr;
};

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onSelectProfilePressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(&ctx->eventGroup, ctx->selectProfileBit);
}

void onOrientationPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->requestedOrientation.store(static_cast<int>(lv_obj_get_index(lv_event_get_target_obj(event))));
    task_event_group_signal(&ctx->eventGroup, ctx->orientationBit);
}

void onRebootPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(&ctx->eventGroup, ctx->rebootBit);
}

lv_obj_t* createRow(lv_obj_t* parent) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    return row;
}

void updateWidgets(Context* ctx) {
    if (ctx->profileLabel == nullptr) {
        return;
    }
    lv_label_set_text(ctx->profileLabel, getProfile(ctx->selectedIndex).name);
    const bool unchanged = ctx->selectedIndex == getActiveProfileIndex() && ctx->selectedOrientation == getActiveOrientation();
    lv_obj_set_state(ctx->rebootButton, LV_STATE_DISABLED, unchanged);
    for (int i = 0; i < ORIENTATION_COUNT; i++) {
        lv_obj_set_state(lv_obj_get_child(ctx->orientationChips, i), LV_STATE_CHECKED, i == static_cast<int>(ctx->selectedOrientation));
    }
    lv_obj_set_hidden(ctx->errorLabel, !ctx->saveFailed);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Simulator");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);

    auto* card = lvgl_card_create(wrapper);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    auto* profile_row = createRow(card);
    auto* profile_column = lv_obj_create(profile_row);
    lv_obj_remove_style_all(profile_column);
    lv_obj_set_height(profile_column, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(profile_column, 1);
    lv_obj_set_flex_flow(profile_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(profile_column, false);

    auto* title_label = lv_label_create(profile_column);
    lv_label_set_text(title_label, "Profile");

    ctx->profileLabel = lv_label_create(profile_column);
    lv_obj_set_style_text_font(ctx->profileLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    auto* list_button = lvgl_icon_button_create(profile_row);
    auto* list_icon = lv_label_create(list_button);
    lv_label_set_text(list_icon, LV_SYMBOL_LIST);
    lv_obj_add_event_cb(list_button, onSelectProfilePressed, LV_EVENT_SHORT_CLICKED, ctx);

    auto* reboot_row = createRow(card);
    auto* reboot_label = lv_label_create(reboot_row);
    lv_label_set_text(reboot_label, "Reboot to apply");
    lv_obj_set_flex_grow(reboot_label, 1);

    ctx->rebootButton = lvgl_text_button_create(reboot_row);
    auto* reboot_button_label = lv_label_create(ctx->rebootButton);
    lv_label_set_text(reboot_button_label, "Reboot");
    lv_obj_add_event_cb(ctx->rebootButton, onRebootPressed, LV_EVENT_SHORT_CLICKED, ctx);

    ctx->errorLabel = lv_label_create(card);
    lv_label_set_text(ctx->errorLabel, "Failed to save the profile");
    lv_obj_set_style_text_color(ctx->errorLabel, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ctx->errorLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    auto* orientation_title = lv_label_create(wrapper);
    lv_label_set_text(orientation_title, "Orientation");

    auto* orientation_card = lvgl_card_create(wrapper);
    lv_obj_set_size(orientation_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(orientation_card, LV_FLEX_FLOW_COLUMN);

    // The pills wrap onto the next row when they don't fit
    ctx->orientationChips = lv_obj_create(orientation_card);
    lv_obj_set_size(ctx->orientationChips, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->orientationChips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_bg_opa(ctx->orientationChips, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->orientationChips, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ctx->orientationChips, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(ctx->orientationChips, false);
    for (const auto* label : ORIENTATION_LABELS) {
        auto* chip = lvgl_chip_create(ctx->orientationChips);
        lv_obj_add_event_cb(chip, onOrientationPressed, LV_EVENT_SHORT_CLICKED, ctx);
        lv_label_set_text(lv_label_create(chip), label);
    }
    // Only the container is in the input group: left and right move between the pills
    lvgl_grid_navigation_add(ctx->orientationChips);

    updateWidgets(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->profileLabel = nullptr;
    ctx->rebootButton = nullptr;
    ctx->orientationChips = nullptr;
    ctx->errorLabel = nullptr;
}

void startProfileDialog(Context* ctx) {
    if (ctx->dialogId != 0) {
        return;
    }
    const char* argv[PROFILE_COUNT + 1] = { "Profile" };
    for (size_t i = 0; i < PROFILE_COUNT; i++) {
        argv[i + 1] = getProfile(i).name;
    }
    AppStartContext start_context;
    if (app_start_context_from_id(SELECTION_DIALOG_ID, &start_context) != ERROR_NONE) {
        return;
    }
    app_start_context_set_arguments_ext(&start_context, PROFILE_COUNT + 1, argv);
    app_start_context_set_parent(&start_context, ctx->appInstanceId);
    app_start_with_context(&start_context, &ctx->dialogId);
}

/** A failed save keeps the selection that the next start loads */
void saveAndShow(Context* ctx, size_t profileIndex, OrientationOverride orientation) {
    ctx->saveFailed = !saveSelection(profileIndex, orientation);
    if (!ctx->saveFailed) {
        ctx->selectedIndex = profileIndex;
        ctx->selectedOrientation = orientation;
    }
    lvgl_lock();
    updateWidgets(ctx);
    lvgl_unlock();
}

// Other results than an index mean that the dialog was dismissed
void onDialogResult(Context* ctx, int32_t selected) {
    ctx->dialogId = 0;
    if (selected < 0 || selected >= static_cast<int32_t>(PROFILE_COUNT)) {
        return;
    }
    // A new profile starts with its own orientation
    saveAndShow(ctx, selected, OrientationOverride::Default);
}

void onOrientationRequest(Context* ctx, int requested) {
    if (requested < 0 || requested >= ORIENTATION_COUNT) {
        return;
    }
    saveAndShow(ctx, ctx->selectedIndex, static_cast<OrientationOverride>(requested));
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx;
    ctx.appInstanceId = app_scheduler_current_app_id();
    ctx.selectedIndex = loadSelectedProfileIndex();
    ctx.selectedOrientation = loadSelectedOrientation();

    task_event_group_construct(&ctx.eventGroup);
    check(task_event_group_claim_bit(&ctx.eventGroup, &ctx.selectProfileBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&ctx.eventGroup, &ctx.rebootBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&ctx.eventGroup, &ctx.orientationBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &ctx.eventGroup) == ERROR_NONE);

    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&ctx.eventGroup, &flags, portMAX_DELAY);

        if (flags & ctx.selectProfileBit) {
            startProfileDialog(&ctx);
        }
        if (flags & ctx.orientationBit) {
            onOrientationRequest(&ctx, ctx.requestedOrientation.exchange(-1));
        }
        if ((flags & ctx.rebootBit) && (ctx.selectedIndex != getActiveProfileIndex() || ctx.selectedOrientation != getActiveOrientation())) {
            reboot();
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
            if (event.type == APP_EVENT_RESULT) {
                if (event.result.launch_id == ctx.dialogId) {
                    onDialogResult(&ctx, event.result.result);
                }
                app_manager_stop(event.result.launch_id);
            }
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&ctx.eventGroup);
    return 0;
}

} // namespace

extern const ::AppManifest profileAppManifest = {
    .id = "tactility.simulator",
    .name = "Simulator",
    .category = APP_CATEGORY_SETTINGS,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {},
};

}
