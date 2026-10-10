#include <Tactility/lvgl/SystemMenu.h>

#include <Tactility/TactilityPrivate.h>
#include <Tactility/bluetooth/Bluetooth.h>

#include <app/manager.h>
#include <app/start.h>

#include <gps/gps.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/concurrent/dispatcher.h>
#include <tactility/device.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <cstdint>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::lvgl {

constexpr auto* TAG = "SystemMenu";
constexpr auto* POWER_OFF_APP_ID = "tactility.poweroff";

static void* toContext(bool enabled) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(enabled));
}

static bool fromContext(void* context) {
    return reinterpret_cast<intptr_t>(context) != 0;
}

static bool hasWifi() {
    return device_exists_of_type(&WIFI_TYPE);
}

static bool hasGps() {
    return device_exists_of_type(&GPS_TYPE);
}

static bool hasBluetooth() {
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    return device_exists_of_type(&BLUETOOTH_TYPE);
#else
    return false;
#endif
}

static bool isWifiEnabled() {
    Device* device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &device) != ERROR_NONE) {
        return false;
    }
    WifiRadioState state = WIFI_RADIO_STATE_OFF;
    wifi_get_radio_state(device, &state);
    device_put(device);
    return state == WIFI_RADIO_STATE_ON || state == WIFI_RADIO_STATE_ON_PENDING;
}

static void setWifiEnabled(void* context) {
    Device* device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &device) != ERROR_NONE) {
        return;
    }
    error_t result = fromContext(context) ? wifi_set_radio_on(device) : wifi_set_radio_off(device);
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to set Wi-Fi radio state (%s)", error_to_string(result));
    }
    device_put(device);
}

static bool isGpsEnabled() {
    bool enabled = false;
    device_for_each_of_type(&GPS_TYPE, &enabled, [](Device* device, void* context) {
        if (device_is_ready(device)) {
            *static_cast<bool*>(context) = true;
            return false;
        }
        return true;
    });
    return enabled;
}

static void setGpsEnabled(void* context) {
    device_for_each_of_type(&GPS_TYPE, context, [](Device* device, void* context) {
        const bool enabled = fromContext(context);
        if (enabled && !device_is_ready(device)) {
            device_start(device);
        } else if (!enabled && device_is_ready(device)) {
            device_stop(device);
        }
        return true;
    });
}

#if defined(CONFIG_BT_NIMBLE_ENABLED)

static bool isBluetoothEnabled() {
    Device* device = nullptr;
    if (device_get_first_by_type(&BLUETOOTH_TYPE, &device) != ERROR_NONE) {
        return false;
    }
    bool enabled = bluetooth::isRadioOnOrPending(device);
    device_put(device);
    return enabled;
}

static void setBluetoothEnabled(void* context) {
    Device* device = nullptr;
    if (device_get_first_by_type(&BLUETOOTH_TYPE, &device) != ERROR_NONE) {
        return;
    }
    const bool enabled = fromContext(context);
    if (enabled != bluetooth::isRadioOnOrPending(device)) {
        if (enabled) {
            bluetooth::start(device);
        } else {
            bluetooth::stop(device);
        }
    }
    device_put(device);
}

#endif

// Changing the radio and device states can block, so it happens on the main task
struct ToggleOption {
    const char* enabledIcon;
    const char* disabledIcon;
    bool (*isEnabled)();
    DispatcherCallback setEnabled;
};

static const char* getToggleIcon(const ToggleOption* option, bool enabled) {
    return enabled ? option->enabledIcon : option->disabledIcon;
}

static void onToggleChanged(lv_event_t* event) {
    auto* toggle = lv_event_get_target_obj(event);
    const auto* option = static_cast<const ToggleOption*>(lv_event_get_user_data(event));
    const bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    lv_label_set_text(lv_obj_get_child(toggle, 0), getToggleIcon(option, enabled));
    dispatcher_dispatch_timed(getMainDispatcherHandle(), toContext(enabled), option->setEnabled, portMAX_DELAY);
}

static void onCloseClicked(lv_event_t* /*event*/) {
    window_manager_overlay_hide();
}

static void onBackdropGesture(lv_event_t* /*event*/) {
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        // The release must not click the pressed widget, e.g. a toggle
        lv_indev_wait_release(indev);
        window_manager_overlay_hide();
    }
}

static bool supportsPowerOff() {
    ::AppManifest manifest;
    if (app_manager_find_manifest(POWER_OFF_APP_ID, &manifest) != ERROR_NONE) {
        return false;
    }
    bool supported = false;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &supported, [](Device* device, void* context) {
        if (device_is_ready(device) && power_supply_supports_power_off(device)) {
            *static_cast<bool*>(context) = true;
            return false;
        }
        return true;
    });
    return supported;
}

static void onPowerOffClicked(lv_event_t* /*event*/) {
    // The app's window is created in the app layer, which the overlay would cover
    window_manager_overlay_hide();
    AppStartContext context;
    uint32_t instance_id = 0;
    if (app_start_context_from_id(POWER_OFF_APP_ID, &context) == ERROR_NONE) {
        app_start_with_context(&context, &instance_id);
    }
}

// A square icon button, sized for the menu's tiles
static lv_obj_t* createTile(lv_obj_t* parent, const char* icon) {
    const auto icon_height = static_cast<int32_t>(lvgl_get_shared_icon_default_font_height());

    auto* tile = lvgl_icon_button_create(parent);
    lv_obj_set_size(tile, icon_height * 2, icon_height * 2);

    auto* icon_label = lv_label_create(tile);
    lv_obj_set_style_text_font(icon_label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(icon_label, icon);
    lv_obj_center(icon_label);
    return tile;
}

// A tile that the theme shows as on while it's checked
static void createOption(lv_obj_t* parent, const ToggleOption* option) {
    const bool enabled = option->isEnabled();
    auto* toggle = createTile(parent, getToggleIcon(option, enabled));
    lv_obj_add_flag(toggle, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_state(toggle, LV_STATE_CHECKED, enabled);
    lv_obj_add_event_cb(toggle, onToggleChanged, LV_EVENT_VALUE_CHANGED, const_cast<ToggleOption*>(option));
}

static void createWidgets(lv_obj_t* parent, void* /*userData*/) {
    // Swiping up anywhere closes the menu
    lv_obj_set_gesture_bubble(parent, false);
    lv_obj_add_event_cb(parent, onBackdropGesture, LV_EVENT_GESTURE, nullptr);

    // The card fits its content, up to most of the screen
    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(card, LV_PCT(90), LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(card);

    auto* grid = lv_obj_create(card);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    // A percentage can't limit the width, as the card's width depends on the grid. The tiles wrap at the limit.
    const int32_t max_card_width = lv_display_get_horizontal_resolution(lv_obj_get_display(parent)) * 9 / 10;
    const int32_t card_padding = lv_obj_get_style_pad_left(card, LV_PART_MAIN) + lv_obj_get_style_pad_right(card, LV_PART_MAIN);
    lv_obj_set_style_max_width(grid, max_card_width - card_padding, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const int32_t gap = lvgl_theme_is_compact() ? 8 : 16;
    lv_obj_set_style_pad_column(grid, gap, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(grid, gap, LV_STATE_DEFAULT);

    static constexpr ToggleOption wifi_option = { LVGL_ICON_SHARED_WIFI, LVGL_ICON_SHARED_WIFI_OFF, isWifiEnabled, setWifiEnabled };
    if (hasWifi()) {
        createOption(grid, &wifi_option);
    }
    static constexpr ToggleOption gps_option = { LVGL_ICON_SHARED_LOCATION_ON, LVGL_ICON_SHARED_LOCATION_OFF, isGpsEnabled, setGpsEnabled };
    if (hasGps()) {
        createOption(grid, &gps_option);
    }
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    static constexpr ToggleOption bluetooth_option = { LVGL_ICON_SHARED_BLUETOOTH, LVGL_ICON_SHARED_BLUETOOTH_DISABLED, isBluetoothEnabled, setBluetoothEnabled };
    if (hasBluetooth()) {
        createOption(grid, &bluetooth_option);
    }
#endif
    if (supportsPowerOff()) {
        auto* power_tile = createTile(grid, LVGL_ICON_SHARED_POWER_SETTINGS_NEW);
        lv_obj_add_event_cb(power_tile, onPowerOffClicked, LV_EVENT_SHORT_CLICKED, nullptr);
    }
    // The arrow keys move between the tiles. Toggles that are focused directly would be switched by them.
    lvgl_grid_navigation_add(grid);

    // Without a pointer device, tapping outside of the card or swiping up isn't possible
    if (!lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
        auto* close_button = createTile(card, LVGL_ICON_SHARED_CLOSE);
        lv_obj_add_event_cb(close_button, onCloseClicked, LV_EVENT_SHORT_CLICKED, nullptr);
    }
}

void systemMenuShow() {
    if (!hasWifi() && !hasGps() && !hasBluetooth() && !supportsPowerOff()) {
        return;
    }
    window_manager_overlay_show(createWidgets, nullptr, nullptr);
}

}
