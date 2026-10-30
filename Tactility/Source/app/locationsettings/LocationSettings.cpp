#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/lvgl/Style.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <gps/gps.h>
#include <gps/gps_settings.h>
#include <gps_generic/gps_generic.h>

#include <location/location.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/device.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <iterator>

namespace tt::app::addgps {
extern const ::AppManifest manifest;
}

namespace tt::app::locationsettings {

extern const ::AppManifest manifest;

namespace {

struct PrecisionOption {
    LocationPrecision precision;
    const char* name;
};

constexpr PrecisionOption PRECISION_OPTIONS[] = {
    { LOCATION_PRECISION_APPROXIMATE, "Approximate" },
    { LOCATION_PRECISION_PRECISE, "Precise" },
};

constexpr uint32_t SUBSCRIBERS_REFRESH_MS = 1000;

/** A GPS device that is backed by a persisted configuration */
struct GpsConfigurationRow {
    Device* device;
    size_t configurationIndex;
};

struct Context {
    uint32_t appInstanceId;
    // GPS devices are only configurable on hardware that can connect them
    bool showGpsDevices = false;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t enableBit = 0;
    uint32_t precisionBit = 0;
    ::std::atomic<bool> requestedEnabled = false;
    ::std::atomic<LocationPrecision> requestedPrecision = LOCATION_PRECISION_PRECISE;
    // Valid while the window is shown
    lv_obj_t* enableSwitch = nullptr;
    lv_obj_t* precisionChips = nullptr;
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* gpsDeviceList = nullptr;
    std::vector<GpsConfigurationRow> gpsConfigurationRows;
    lv_obj_t* subscribersSection = nullptr;
    lv_obj_t* subscribersList = nullptr;
    // Filled by the app's task. The list shows shownSubscribers.
    std::vector<std::string> subscribers;
    std::vector<std::string> shownSubscribers;
    // Set while a delete confirmation is pending
    bool hasPendingDelete = false;
    size_t pendingDeleteIndex = 0;
};

LocationPrecision getChipPrecision(lv_obj_t* chip) {
    return static_cast<LocationPrecision>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(chip)));
}

std::string getStatusText() {
    if (!location_is_enabled()) {
        return "Location services are off";
    }
    Location location;
    if (location_get(&location) != ERROR_NONE) {
        return "Waiting for location...";
    }
    std::string text;
    auto output = ::std::back_inserter(text);
    ::std::format_to(output, "{:.5f}, {:.5f}", location.latitude, location.longitude);
    if (!::std::isnan(location.horizontal_accuracy)) {
        ::std::format_to(output, " (±{:.0f} m)", location.horizontal_accuracy);
    }
    ::std::format_to(output, "\nSource: {}", location.provider);
    return text;
}

void readSubscribers(Context* ctx) {
    ctx->subscribers.clear();
    location_for_each_subscriber(&ctx->subscribers, [](const char* description, void* context) {
        static_cast<std::vector<std::string>*>(context)->push_back(description);
    });
}

void updateSubscribers(Context* ctx) {
    if (ctx->subscribersSection == nullptr || ctx->subscribers == ctx->shownSubscribers) {
        return;
    }
    ctx->shownSubscribers = ctx->subscribers;
    lv_obj_clean(ctx->subscribersList);
    for (const auto& subscriber : ctx->shownSubscribers) {
        lvgl_list_add_button(ctx->subscribersList, nullptr, subscriber.c_str());
    }
    lvgl_list_hide_last_divider(ctx->subscribersList);
    lv_obj_set_flag(ctx->subscribersSection, LV_OBJ_FLAG_HIDDEN, ctx->shownSubscribers.empty());
}

void updateWidgets(Context* ctx) {
    if (ctx->statusLabel == nullptr) {
        return;
    }
    lv_obj_set_state(ctx->enableSwitch, LV_STATE_CHECKED, location_is_enabled());

    const LocationPrecision precision = location_get_precision();
    const uint32_t count = lv_obj_get_child_count(ctx->precisionChips);
    for (uint32_t i = 0; i < count; i++) {
        auto* chip = lv_obj_get_child(ctx->precisionChips, static_cast<int32_t>(i));
        lv_obj_set_state(chip, LV_STATE_CHECKED, getChipPrecision(chip) == precision);
    }

    lv_label_set_text(ctx->statusLabel, getStatusText().c_str());
    updateSubscribers(ctx);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

// Starting and stopping the service blocks, so it happens on the app's own task
void onEnableSwitchChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->requestedEnabled = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    task_event_group_signal(ctx->eventGroup, ctx->enableBit);
}

void onPrecisionChipPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->requestedPrecision = getChipPrecision(lv_event_get_target_obj(event));
    task_event_group_signal(ctx->eventGroup, ctx->precisionBit);
}

lv_obj_t* createContainer(lv_obj_t* parent, lv_flex_flow_t flow) {
    auto* container = lv_obj_create(parent);
    lv_obj_set_flex_flow(container, flow);
    lv_obj_set_style_border_width(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(container, false);
    return container;
}

void onAddGpsPressed(lv_event_t*) {
    AppStartContext context = app_start_context_for_manifest(&addgps::manifest);
    uint32_t instanceId = 0;
    app_start_with_context(&context, &instanceId);
}

void onDeleteGpsPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* device = static_cast<Device*>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
    for (const auto& row : ctx->gpsConfigurationRows) {
        if (row.device == device) {
            ctx->pendingDeleteIndex = row.configurationIndex;
            ctx->hasPendingDelete = true;
            alertdialog::start(ctx->appInstanceId, "Confirmation", ::std::string("Do you want to delete ") + device->name + "?", ::std::vector<::std::string> { "Yes", "No" });
            return;
        }
    }
}

/**
 * Finds the persisted configuration of a device by the name of its UART.
 * The index identifies the entry for gps_settings_remove_configuration_at(), even when entries are identical.
 */
bool findConfigurationIndex(Device* device, size_t& outIndex) {
    auto* parent = device_get_parent(device);
    if (parent == nullptr) {
        return false;
    }

    struct FindContext {
        const char* uartName;
        size_t* outIndex;
        bool found;
    } findContext = { parent->name, &outIndex, false };

    gps_settings_for_each_configuration(&findContext, [](const GpsConfiguration* configuration, size_t index, void* untypedContext) {
        auto* context = static_cast<FindContext*>(untypedContext);
        if (!context->found && strcmp(configuration->uart_name, context->uartName) == 0) {
            *context->outIndex = index;
            context->found = true;
        }
    });
    return findContext.found;
}

/** @return the bus name and speed, e.g. "ttyACM0 9600 bps" */
std::string getConnectionDescription(Device* device) {
    auto* parent = device_get_parent(device);
    std::string result = parent != nullptr ? parent->name : "";
    if (device_is_compatible(device, "tactility,gps-generic")) {
        const auto* config = static_cast<const GpsConfig*>(device->config);
        ::std::format_to(::std::back_inserter(result), " {} bps", config->baud_rate);
    }
    return result;
}

void createGpsDeviceRow(Context* ctx, Device* device) {
    auto* row = createContainer(ctx->gpsDeviceList, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // The texts take the width that the button leaves. Texts that don't fit scroll.
    auto* texts = createContainer(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_style_pad_row(texts, 0, LV_STATE_DEFAULT);

    auto* nameLabel = lv_label_create(texts);
    lv_obj_set_width(nameLabel, LV_PCT(100));
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    char modelName[64];
    lv_label_set_text(nameLabel, gps_get_model_name(device, modelName, sizeof(modelName)) == ERROR_NONE ? modelName : device->name);

    auto* connectionLabel = lv_label_create(texts);
    lv_obj_set_width(connectionLabel, LV_PCT(100));
    lv_label_set_long_mode(connectionLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(connectionLabel, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_label_set_text(connectionLabel, getConnectionDescription(device).c_str());

    // Devicetree devices have no persisted configuration and can't be deleted
    size_t configurationIndex;
    if ((device->flags & DEVICE_FLAG_DYNAMIC) && findConfigurationIndex(device, configurationIndex)) {
        auto* deleteButton = lv_button_create(row);
        lv_obj_set_user_data(deleteButton, device);
        lv_obj_add_event_cb(deleteButton, onDeleteGpsPressed, LV_EVENT_SHORT_CLICKED, ctx);
        auto* deleteLabel = lv_label_create(deleteButton);
        lv_obj_set_style_text_font(deleteLabel, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
        lv_label_set_text(deleteLabel, LVGL_ICON_SHARED_DELETE);
        ctx->gpsConfigurationRows.push_back({ .device = device, .configurationIndex = configurationIndex });
    }
}

void rebuildGpsDeviceList(Context* ctx) {
    ctx->gpsConfigurationRows.clear();
    if (ctx->gpsDeviceList == nullptr) {
        return;
    }
    lv_obj_clean(ctx->gpsDeviceList);

    bool hasDevices = false;
    struct ForEachContext {
        Context* ctx;
        bool* hasDevices;
    } forEachContext = { ctx, &hasDevices };
    device_for_each_of_type(&GPS_TYPE, &forEachContext, [](Device* device, void* context) {
        auto* forEach = static_cast<ForEachContext*>(context);
        createGpsDeviceRow(forEach->ctx, device);
        *forEach->hasDevices = true;
        return true;
    });

    if (!hasDevices) {
        lv_label_set_text(lv_label_create(ctx->gpsDeviceList), "No devices");
    }
}

void createGpsDevices(Context* ctx, lv_obj_t* parent) {
    auto* header = createContainer(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* title = lv_label_create(header);
    lv_obj_set_flex_grow(title, 1);
    lv_label_set_text(title, "GPS devices");

    auto* addButton = lv_button_create(header);
    lv_obj_add_event_cb(addButton, onAddGpsPressed, LV_EVENT_SHORT_CLICKED, ctx);
    lv_label_set_text(lv_label_create(addButton), LV_SYMBOL_PLUS);

    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(title, lv_obj_get_style_pad_left(card, LV_PART_MAIN), LV_STATE_DEFAULT);

    ctx->gpsDeviceList = createContainer(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(ctx->gpsDeviceList, LV_PCT(100), LV_SIZE_CONTENT);
    rebuildGpsDeviceList(ctx);
}

void createSubscribers(Context* ctx, lv_obj_t* parent) {
    ctx->subscribersSection = createContainer(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(ctx->subscribersSection, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_flag(ctx->subscribersSection, LV_OBJ_FLAG_HIDDEN);

    auto* title = lv_label_create(ctx->subscribersSection);
    lv_label_set_text(title, "Subscribers");

    auto* card = lvgl_card_create(ctx->subscribersSection);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(title, lv_obj_get_style_pad_left(card, LV_PART_MAIN), LV_STATE_DEFAULT);

    ctx->subscribersList = lvgl_list_create(card);
    ctx->shownSubscribers.clear();
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Location");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    ctx->enableSwitch = lvgl_toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(ctx->enableSwitch, onEnableSwitchChanged, LV_EVENT_VALUE_CHANGED, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lvgl::obj_set_style_bg_invisible(wrapper);

    auto* precisionLabel = lv_label_create(wrapper);
    lv_label_set_text(precisionLabel, "Precision");

    // The chips are centered in the card while they fit, and scroll when they don't
    auto* precisionCard = lvgl_card_create(wrapper);
    lv_obj_set_size(precisionCard, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(precisionCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(precisionCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(precisionLabel, lv_obj_get_style_pad_left(precisionCard, LV_PART_MAIN), LV_STATE_DEFAULT);

    ctx->precisionChips = lv_obj_create(precisionCard);
    lv_obj_set_size(ctx->precisionChips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(ctx->precisionChips, LV_PCT(100), LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(ctx->precisionChips, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(ctx->precisionChips, LV_DIR_HOR);
    lv_obj_set_style_bg_opa(ctx->precisionChips, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->precisionChips, 0, LV_STATE_DEFAULT);
    // The chips' margins leave room for their focus rings and space them apart
    lv_obj_set_style_pad_all(ctx->precisionChips, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ctx->precisionChips, 0, LV_STATE_DEFAULT);

    for (const auto& option : PRECISION_OPTIONS) {
        auto* chip = lvgl_chip_create(ctx->precisionChips);
        lv_label_set_text(lv_label_create(chip), option.name);
        lv_obj_set_user_data(chip, reinterpret_cast<void*>(static_cast<intptr_t>(option.precision)));
        lv_obj_add_event_cb(chip, onPrecisionChipPressed, LV_EVENT_SHORT_CLICKED, ctx);
    }
    lvgl_grid_navigation_add(ctx->precisionChips);

    auto* precisionHint = lv_label_create(precisionCard);
    lv_obj_set_width(precisionHint, LV_PCT(100));
    lv_label_set_long_mode(precisionHint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(precisionHint, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    lv_label_set_text(precisionHint, "Approximate location is accurate to a few kilometers. Location based on the IP address is never precise.");

    auto* statusCard = lvgl_card_create(wrapper);
    lv_obj_set_size(statusCard, LV_PCT(100), LV_SIZE_CONTENT);
    ctx->statusLabel = lv_label_create(statusCard);
    lv_obj_set_width(ctx->statusLabel, LV_PCT(100));
    lv_label_set_long_mode(ctx->statusLabel, LV_LABEL_LONG_WRAP);

    if (ctx->showGpsDevices) {
        createGpsDevices(ctx, wrapper);
    }
    createSubscribers(ctx, wrapper);

    updateWidgets(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->enableSwitch = nullptr;
    ctx->precisionChips = nullptr;
    ctx->statusLabel = nullptr;
    ctx->gpsDeviceList = nullptr;
    ctx->gpsConfigurationRows.clear();
    ctx->subscribersSection = nullptr;
    ctx->subscribersList = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = app_scheduler_current_app_id();
    ::AppManifest addGpsManifest;
    ctx.showGpsDevices = app_manager_find_manifest(addgps::manifest.id, &addGpsManifest) == ERROR_NONE;

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    ctx.eventGroup = &eventGroup;
    check(task_event_group_claim_bit(&eventGroup, &ctx.enableBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&eventGroup, &ctx.precisionBit) == ERROR_NONE);

    AppEventSubscription appEvents {};
    check(app_event_subscribe(&appEvents, &eventGroup) == ERROR_NONE);
    LocationSubscription locationEvents {};
    check(location_subscribe(&locationEvents, &eventGroup, "Location settings") == ERROR_NONE);

    readSubscribers(&ctx);
    WindowId window = window_manager_create_ext(ctx.appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        // Subscribers come and go without signaling, so the list is refreshed regularly
        task_event_group_wait_any(&eventGroup, &flags, pdMS_TO_TICKS(SUBSCRIBERS_REFRESH_MS));

        if (flags & ctx.enableBit) {
            location_set_enabled(ctx.requestedEnabled);
        }
        if (flags & ctx.precisionBit) {
            location_set_precision(ctx.requestedPrecision);
        }

        AppEvent event {};
        while (app_event_poll(&appEvents, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
            if (event.type == APP_EVENT_RESULT) {
                if (ctx.hasPendingDelete) {
                    ctx.hasPendingDelete = false;
                    // The first button is "Yes"
                    if (event.result.result == 0) {
                        gps_settings_remove_configuration_at(ctx.pendingDeleteIndex);
                        lvgl_lock();
                        rebuildGpsDeviceList(&ctx);
                        lvgl_unlock();
                    }
                }
                app_manager_stop(event.result.launch_id);
            }
        }

        lvgl_lock();
        readSubscribers(&ctx);
        updateWidgets(&ctx);
        lvgl_unlock();
    }

    window_manager_remove(window);
    check(location_unsubscribe(&locationEvents) == ERROR_NONE);
    check(app_event_unsubscribe(&appEvents) == ERROR_NONE);
    task_event_group_destruct(&eventGroup);
    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.locationsettings",
    .name = "Location",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

}
