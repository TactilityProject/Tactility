#define L10N_APP "addgps"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/app/selectiondialog/SelectionDialog.h>
#include <Tactility/lvgl/Style.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>
#include <tactility/check.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/log.h>

#include <gps/gps.h>
#include <gps/gps_settings.h>

#include <array>
#include <cstring>
#include <lvgl.h>
#include <string>
#include <vector>

namespace tt::app::addgps {

constexpr auto* TAG = "AddGps";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t selectUartBit = 0;
    uint32_t selectModelBit = 0;
    uint32_t selectBaudBit = 0;
    uint32_t uartSelectLaunchId = 0;
    uint32_t modelSelectLaunchId = 0;
    uint32_t baudSelectLaunchId = 0;

    // Committed state, which survives widget rebuilds
    std::string uartName;
    size_t modelIndex = 0;
    size_t baudIndex = 0;

    // Valid while the window is shown
    lv_obj_t* uartValueLabel = nullptr;
    lv_obj_t* modelValueLabel = nullptr;
    lv_obj_t* baudValueLabel = nullptr;

    // The bus names that the open bus selection dialog shows
    std::vector<std::string> uartNames;

    std::array<uint32_t, 6> baudRates = { 9600, 19200, 28800, 38400, 57600, 115200 };
};


std::vector<std::string> getModelNames() {
    std::vector<std::string> result;
    for (int model = GpsModel::GPS_MODEL_UNKNOWN; model <= GpsModel::GPS_MODEL_UC6580; model++) {
        // GPS_MODEL_UNKNOWN makes the driver probe for the model
        result.emplace_back(model == GPS_MODEL_UNKNOWN ? tr("Auto-detect") : gps_model_to_string(static_cast<GpsModel>(model)));
    }
    return result;
}

std::vector<std::string> getBaudNames(Context* ctx) {
    std::vector<std::string> result;
    for (auto baud_rate : ctx->baudRates) {
        result.push_back(std::to_string(baud_rate));
    }
    return result;
}

/** Requires the LVGL lock */
void updateViews(Context* ctx) {
    if (ctx->modelValueLabel == nullptr) {
        return;
    }
    lv_label_set_text(ctx->uartValueLabel, ctx->uartName.empty() ? tr("None") : ctx->uartName.c_str());
    lv_label_set_text(ctx->modelValueLabel, getModelNames()[ctx->modelIndex].c_str());
    lv_label_set_text(ctx->baudValueLabel, std::to_string(ctx->baudRates[ctx->baudIndex]).c_str());
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onSelectUartPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectUartBit);
}

void onSelectModelPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectModelBit);
}

void onSelectBaudPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectBaudBit);
}

void onAddGpsPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));

    GpsConfiguration new_configuration = {
        .uart_name = { 0x00 },
        .baud_rate = ctx->baudRates[ctx->baudIndex],
        // Warning: This assumes that the enum is a regularly indexed one that starts at 0
        .model = (GpsModel)ctx->modelIndex
    };

    strncpy(new_configuration.uart_name, ctx->uartName.c_str(), sizeof(new_configuration.uart_name) - 1);
    if (new_configuration.uart_name[0] == 0x00) {
        alertdialog::start(ctx->appInstanceId, tr("Error"), tr("You must select a bus/uart."));
        return;
    }

    LOG_I(TAG, "Saving: uart=%s, model=%d, baud=%u", new_configuration.uart_name, (int)new_configuration.model, (unsigned)new_configuration.baud_rate);
    error_t error = gps_settings_add_configuration(&new_configuration);
    if (error == ERROR_ALREADY_EXISTS) {
        alertdialog::start(ctx->appInstanceId, tr("Error"), tr("A GPS is already configured for this bus."));
    } else if (error != ERROR_NONE) {
        alertdialog::start(ctx->appInstanceId, tr("Error"), tr("Failed to add configuration"));
    } else {
        onBackPressed(event);
    }
}

std::vector<std::string> getUartNames() {
    std::vector<std::string> names;
    device_for_each_of_type(&UART_CONTROLLER_TYPE, &names, [](auto* device, auto* context) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(device->name);
        return true;
    });
    return names;
}

/**
 * Creates a row with a title, the selected value below it, and a tr("Select") button.
 * @return the label of the selected value
 */
lv_obj_t* createSelectRow(lv_obj_t* parent, const char* title, lv_event_cb_t onSelectPressed, Context* ctx) {
    // Transparent, so the card provides the background
    auto* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(row, false);

    // The texts take the width that the button leaves. Texts that don't fit scroll.
    auto* texts = lv_obj_create(row);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(texts, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(texts, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(texts, false);

    auto* title_label = lv_label_create(texts);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(title_label, title);

    auto* value_label = lv_label_create(texts);
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(value_label, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);

    auto* select_button = lv_button_create(row);
    lv_label_set_text(lv_label_create(select_button), tr("Select"));
    lv_obj_add_event_cb(select_button, onSelectPressed, LV_EVENT_SHORT_CLICKED, ctx);

    return value_label;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, tr("Add GPS"));
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(main_wrapper, 0, 0);
    lvgl::obj_set_style_bg_invisible(main_wrapper);

    auto* card = lvgl_card_create(main_wrapper);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    ctx->uartValueLabel = createSelectRow(card, tr("Bus"), onSelectUartPressed, ctx);
    ctx->modelValueLabel = createSelectRow(card, tr("Model"), onSelectModelPressed, ctx);
    ctx->baudValueLabel = createSelectRow(card, tr("Baud"), onSelectBaudPressed, ctx);

    // region Button

    auto* add_button = lv_button_create(main_wrapper);
    lv_label_set_text(lv_label_create(add_button), tr("Add"));
    lv_obj_add_event_cb(add_button, onAddGpsPressed, LV_EVENT_SHORT_CLICKED, ctx);

    // endregion

    updateViews(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->uartValueLabel = nullptr;
    ctx->modelValueLabel = nullptr;
    ctx->baudValueLabel = nullptr;
}

void startSelection(Context* ctx, uint32_t& launchId, const char* title, const std::vector<std::string>& items) {
    if (launchId != 0) {
        return;
    }
    launchId = selectiondialog::start(ctx->appInstanceId, title, items);
}

/** @return the selected index, or -1 when the dialog was dismissed */
int32_t getSelectionResult(const AppEvent& event, size_t itemCount) {
    const int32_t selected = event.result.result;
    return (selected >= 0 && selected < static_cast<int32_t>(itemCount)) ? selected : -1;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.selectUartBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.selectModelBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.selectBaudBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if (flags & ctx.selectUartBit && ctx.uartSelectLaunchId == 0) {
            ctx.uartNames = getUartNames();
            if (ctx.uartNames.empty()) {
                alertdialog::start(ctx.appInstanceId, tr("Error"), tr("No bus/uart found."));
            } else {
                startSelection(&ctx, ctx.uartSelectLaunchId, tr("Bus"), ctx.uartNames);
            }
        }
        if (flags & ctx.selectModelBit) {
            startSelection(&ctx, ctx.modelSelectLaunchId, tr("Model"), getModelNames());
        }
        if (flags & ctx.selectBaudBit) {
            startSelection(&ctx, ctx.baudSelectLaunchId, tr("Baud"), getBaudNames(&ctx));
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT:
                    if (event.result.launch_id == ctx.uartSelectLaunchId) {
                        ctx.uartSelectLaunchId = 0;
                        auto selected = getSelectionResult(event, ctx.uartNames.size());
                        if (selected >= 0) ctx.uartName = ctx.uartNames[selected];
                    } else if (event.result.launch_id == ctx.modelSelectLaunchId) {
                        ctx.modelSelectLaunchId = 0;
                        auto selected = getSelectionResult(event, getModelNames().size());
                        if (selected >= 0) ctx.modelIndex = selected;
                    } else if (event.result.launch_id == ctx.baudSelectLaunchId) {
                        ctx.baudSelectLaunchId = 0;
                        auto selected = getSelectionResult(event, ctx.baudRates.size());
                        if (selected >= 0) ctx.baudIndex = selected;
                    }
                    lvgl_lock();
                    updateViews(&ctx);
                    lvgl_unlock();
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.addgps",
    .name = "Add GPS",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
