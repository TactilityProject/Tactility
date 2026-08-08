#include <Tactility/StringUtils.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/lvgl/Style.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/icons/shared.h>
#include <lvgl/widgets/toolbar.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/log.h>

#include <gps/gps.h>
#include <gps/gps_settings.h>

#include <cstring>
#include <lvgl.h>

namespace tt::app::addgps {

constexpr auto* TAG = "AddGps";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    lv_obj_t* uartDropdown = nullptr;
    lv_obj_t* modelDropdown = nullptr;
    lv_obj_t* baudDropdown = nullptr;

    std::vector<::Device*> devices;

    // Store as string instead of int, so app startup doesn't require parsing all entries.
    // We only need to parse back to int when adding the new GPS entry
    std::array<uint32_t, 6> baudRates = { 9600, 19200, 28800, 38400, 57600, 115200 };
    const char* baudRatesDropdownValues = "9600\n19200\n28800\n38400\n57600\n115200";
};


std::vector<std::string> getModelNames() {
    std::vector<std::string> result;
    for (int model = GpsModel::GPS_MODEL_UNKNOWN; model <= GpsModel::GPS_MODEL_UC6580; model++) {
        result.emplace_back(gps_model_to_string(static_cast<GpsModel>(model)));
    }
    return result;
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onAddGpsPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto selected_baud_index = lv_dropdown_get_selected(ctx->baudDropdown);

    GpsConfiguration new_configuration = {
        .uart_name = { 0x00 },
        .baud_rate = ctx->baudRates[selected_baud_index],
        // Warning: This assumes that the enum is a regularly indexed one that starts at 0
        .model = (GpsModel)lv_dropdown_get_selected(ctx->modelDropdown)
    };

    lv_dropdown_get_selected_str(ctx->uartDropdown, new_configuration.uart_name, sizeof(new_configuration.uart_name));
    if (new_configuration.uart_name[0] == 0x00) {
        alertdialog::start(ctx->appInstanceId, "Error", "You must select a bus/uart.");
        return;
    }

    LOG_I(TAG, "Saving: uart=%s, model=%d, baud=%u", new_configuration.uart_name, (int)new_configuration.model, (unsigned)new_configuration.baud_rate);
    if (gps_settings_add_configuration(&new_configuration) != ERROR_NONE) {
        alertdialog::start(ctx->appInstanceId, "Error", "Failed to add configuration");
    } else {
        onBackPressed(event);
    }
}

void updateUartDevices(Context* ctx) {
    ctx->devices.clear();
    device_for_each_of_type(&UART_CONTROLLER_TYPE, &ctx->devices, [](auto* device, auto* context) {
        auto* vector_ptr = static_cast<std::vector<::Device*>*>(context);
        vector_ptr->push_back(device);
        return true;
    });
}

std::string getUartDropdownNames(Context* ctx) {
    std::vector<std::string> names;
    names.push_back("");
    for (auto* device: ctx->devices) {
        names.push_back(device->name);
    }
    return string::join(names, "\n");
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Add GPS");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(main_wrapper, 0, 0);
    lv_obj_set_style_border_width(main_wrapper, 0, 0);
    lvgl::obj_set_style_bg_invisible(main_wrapper);

    // region Uart

    auto* uart_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(uart_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(uart_wrapper, 0, 0);
    lv_obj_set_style_border_width(uart_wrapper, 0, 0);
    lvgl::obj_set_style_bg_invisible(uart_wrapper);

    ctx->uartDropdown = lv_dropdown_create(uart_wrapper);

    updateUartDevices(ctx);

    auto uart_options = getUartDropdownNames(ctx);
    lv_dropdown_set_options(ctx->uartDropdown, uart_options.c_str());
    lv_obj_align(ctx->uartDropdown, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_width(ctx->uartDropdown, LV_PCT(50));

    auto* uart_label = lv_label_create(uart_wrapper);
    lv_obj_align(uart_label, LV_ALIGN_TOP_LEFT, 0, 10);
    lv_label_set_text(uart_label, "Bus");

    // region Model

    auto* model_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(model_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(model_wrapper, 0, 0);
    lv_obj_set_style_border_width(model_wrapper, 0, 0);
    lvgl::obj_set_style_bg_invisible(model_wrapper);

    ctx->modelDropdown = lv_dropdown_create(model_wrapper);

    auto model_names = getModelNames();
    auto model_options = string::join(model_names, "\n");
    lv_dropdown_set_options(ctx->modelDropdown, model_options.c_str());
    lv_obj_align(ctx->modelDropdown, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_width(ctx->modelDropdown, LV_PCT(50));

    auto* model_label = lv_label_create(model_wrapper);
    lv_obj_align(model_label, LV_ALIGN_TOP_LEFT, 0, 10);
    lv_label_set_text(model_label, "Model");

    // endregion

    // region Baud
    auto* baud_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(baud_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(baud_wrapper, 0, 0);
    lv_obj_set_style_border_width(baud_wrapper, 0, 0);
    lvgl::obj_set_style_bg_invisible(baud_wrapper);

    ctx->baudDropdown = lv_dropdown_create(baud_wrapper);
    lv_dropdown_set_options(ctx->baudDropdown, ctx->baudRatesDropdownValues);
    lv_obj_align(ctx->baudDropdown, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_width(ctx->baudDropdown, LV_PCT(50));

    auto* baud_rate_label = lv_label_create(baud_wrapper);
    lv_obj_align(baud_rate_label, LV_ALIGN_TOP_LEFT, 0, 10);
    lv_label_set_text(baud_rate_label, "Baud");

    // endregion

    // region Button

    auto* button_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(button_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(button_wrapper, 0, 0);
    lv_obj_set_style_border_width(button_wrapper, 0, 0);
    lvgl::obj_set_style_bg_invisible(button_wrapper);

    auto* add_button = lv_button_create(button_wrapper);
    lv_obj_align(add_button, LV_ALIGN_TOP_MID, 0, 0);
    auto* add_label = lv_label_create(add_button);
    lv_label_set_text(add_label, "Add");
    lv_obj_add_event_cb(add_button, onAddGpsPressed, LV_EVENT_SHORT_CLICKED, ctx);

    // endregion
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        switch (event.type) {
            case APP_EVENT_CLOSE:
                app_manager_finish(appInstanceId);
                shouldClose = true;
                break;
            case APP_EVENT_RESULT:
                app_manager_stop(event.result.launch_id);
                break;
            default:
                break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "AddGps",
    .name = "Add GPS",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
