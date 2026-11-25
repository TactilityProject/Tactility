#include <Tactility/lvgl/SystemMenu.h>

#include <Tactility/TactilityPrivate.h>
#include <Tactility/lvgl/SystemMenuPower.h>
#include <Tactility/lvgl/NotificationPanel.h>
#include <Tactility/service/audio/Audio.h>
#include <Tactility/settings/DisplaySettings.h>
#include <Tactility/settings/RadioSettings.h>

#include <app/manager.h>
#include <app/start.h>

#include <location/location.h>

#include <bluetooth/bluetooth_service.h>
#include <bluetooth/bluetooth_settings.h>
#include <wifi/wifi_settings.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/text_button.h>

#include <lvgl_window_manager/system_bars.h>
#include <lvgl_window_manager/window_manager.h>

#include <mesh/mesh.h>
#include <notification/notification.h>

#include <localisation/tr.h>

#include <tactility/Memory.hpp>
#include <tactility/concurrent/dispatcher.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::lvgl {

constexpr auto* TAG = "SystemMenu";
/** The narrowest landscape display that gets a narrower menu, in pixels */
constexpr int32_t WIDE_DISPLAY_MIN_WIDTH = 600;

static void* toContext(bool enabled) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(enabled));
}

static bool fromContext(void* context) {
    return reinterpret_cast<intptr_t>(context) != 0;
}

static bool hasWifi() {
    return device_exists_of_type(&WIFI_TYPE);
}

static bool hasBluetooth() {
    return device_exists_of_type(&BLUETOOTH_TYPE);
}

/** Mesh can be toggled once it was configured: a backend is selected and the region is set */
static bool hasMesh() {
    char backend[MESH_BACKEND_ID_MAX_LENGTH + 1];
    return mesh_is_available() &&
        mesh_get_backend(backend, sizeof(backend)) == ERROR_NONE &&
        settings::radio::loadRegion() != RADIO_REGION_UNSET;
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
    const bool enabled = fromContext(context);
    error_t result = enabled ? wifi_set_radio_on(device) : wifi_set_radio_off(device);
    if (result == ERROR_NONE) {
        if (wifi_settings_set_enable(enabled) != ERROR_NONE) {
            LOG_E(TAG, "Failed to save the Wi-Fi state");
        }
    } else {
        LOG_E(TAG, "Failed to set Wi-Fi radio state (%s)", error_to_string(result));
    }
    device_put(device);
}

static bool isLocationEnabled() {
    return location_is_enabled();
}

static void setLocationEnabled(void* context) {
    const error_t result = location_set_enabled(fromContext(context));
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to set location services state (%s)", error_to_string(result));
    }
}

static bool isBluetoothEnabled() {
    Device* device = nullptr;
    if (device_get_first_by_type(&BLUETOOTH_TYPE, &device) != ERROR_NONE) {
        return false;
    }
    bool enabled = bluetooth_is_radio_on_or_pending(device);
    device_put(device);
    return enabled;
}

static void setBluetoothEnabled(void* context) {
    Device* device = nullptr;
    if (device_get_first_by_type(&BLUETOOTH_TYPE, &device) != ERROR_NONE) {
        return;
    }
    const bool enabled = fromContext(context);
    bluetooth_settings_set_enable(enabled);
    if (enabled != bluetooth_is_radio_on_or_pending(device)) {
        bluetooth_service_set_radio_enabled(enabled);
    }
    device_put(device);
}


static bool isMeshEnabled() {
    return mesh_is_enabled();
}

static void setMeshEnabled(void* context) {
    const error_t result = mesh_set_enabled(fromContext(context));
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to set mesh state (%s)", error_to_string(result));
    }
}


// Changing the radio and device states can block, so it happens on a worker thread
constexpr size_t TOGGLE_WORKER_STACK_SIZE = 8192;

struct ToggleWorker {
    DispatcherHandle_t dispatcher = nullptr;
    ::Thread* thread = nullptr;
};

static int32_t toggleWorkerMain(void* context) {
    auto* worker = static_cast<ToggleWorker*>(context);
    for (;;) {
        dispatcher_consume(worker->dispatcher);
    }
}

/** Starts the worker on the first call and never stops it. Only call this from the LVGL thread. */
static ToggleWorker* getToggleWorker() {
    static ToggleWorker* worker = nullptr;
    if (worker != nullptr) {
        return worker;
    }

    auto* created = newObject<ToggleWorker>();
    if (created == nullptr) {
        LOG_E(TAG, "Failed to allocate the toggle worker");
        return nullptr;
    }
    created->dispatcher = dispatcher_alloc();
    created->thread = thread_alloc_full("system_menu", TOGGLE_WORKER_STACK_SIZE, toggleWorkerMain, created, tskNO_AFFINITY);
    if (created->dispatcher == nullptr || created->thread == nullptr || thread_start(created->thread) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start the toggle worker");
        if (created->thread != nullptr) thread_free(created->thread);
        if (created->dispatcher != nullptr) dispatcher_free(created->dispatcher);
        deleteObject(created);
        return nullptr;
    }
    worker = created;
    return worker;
}

struct ToggleOption {
    const char* enabledIcon;
    const char* disabledIcon;
    bool (*isEnabled)();
    DispatcherCallback setEnabled;
    /** The app that a long press opens */
    const char* settingsAppId;
};

static const char* getToggleIcon(const ToggleOption* option, bool enabled) {
    return enabled ? option->enabledIcon : option->disabledIcon;
}

static void onToggleChanged(lv_event_t* event) {
    auto* toggle = lv_event_get_target_obj(event);
    const auto* option = static_cast<const ToggleOption*>(lv_event_get_user_data(event));
    const bool enabled = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    lv_label_set_text(lv_obj_get_child(toggle, 0), getToggleIcon(option, enabled));
    auto* worker = getToggleWorker();
    if (worker == nullptr || dispatcher_dispatch(worker->dispatcher, toContext(enabled), option->setEnabled) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue the toggle");
    }
}

static void onToggleLongPressed(lv_event_t* event) {
    const auto* option = static_cast<const ToggleOption*>(lv_event_get_user_data(event));
    // The release must not toggle the option
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    // The app's window is created in the app layer, which the overlay would cover
    window_manager_overlay_hide();
    if (app_manager_is_running(option->settingsAppId)) {
        return;
    }
    AppStartContext context;
    uint32_t instance_id = 0;
    if (app_start_context_from_id(option->settingsAppId, &context) == ERROR_NONE) {
        app_start_with_context(&context, &instance_id);
    }
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

/** Twice the icon at regular size, and 1.5 times at the most compact, as the tiles take much of a small display */
static int32_t getTileSize() {
    const auto icon_height = static_cast<int32_t>(lvgl_get_shared_icon_default_font_height());
    return lvgl_theme_lerp(icon_height * 3 / 2, icon_height * 2, lvgl_theme_get_size());
}

// A square icon button, sized for the menu's tiles
static lv_obj_t* createTile(lv_obj_t* parent, const char* icon) {
    const int32_t size = getTileSize();
    auto* tile = lvgl_icon_button_create(parent);
    lv_obj_set_size(tile, size, size);

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
    lv_obj_set_checkable(toggle, true);
    lv_obj_set_state(toggle, LV_STATE_CHECKED, enabled);
    lv_obj_add_event_cb(toggle, onToggleChanged, LV_EVENT_VALUE_CHANGED, const_cast<ToggleOption*>(option));
    lv_obj_add_event_cb(toggle, onToggleLongPressed, LV_EVENT_LONG_PRESSED, const_cast<ToggleOption*>(option));
}

/** The menu shows either the quick settings or the notifications */
enum class Kind { Settings, Notifications };

/** The overlay's data holds the kind in its lowest bits, and this flag to show the power options instead of the quick settings */
constexpr intptr_t KIND_MASK = 0xFF;
constexpr intptr_t SHOW_POWER_OPTIONS = 0x100;

/** A row "[icon] [-] [slider or percentage] [+]" that sets a level from min to 100 */
struct LevelControl {
    struct StepArgument {
        LevelControl* control = nullptr;
        int32_t direction = 0;
    };

    lv_obj_t* root = nullptr;
    lv_obj_t* slider = nullptr;
    lv_obj_t* label = nullptr;
    int32_t labelWidth = 0;
    int32_t min = 0;
    int32_t step = 10;
    /** Applies the level, with the LVGL lock held */
    void (*apply)(int32_t level) = nullptr;
    /** Called when the user is done changing the level. Optional. */
    void (*commit)() = nullptr;
    StepArgument minus;
    StepArgument plus;
};

/** Only one menu exists at a time, as it is the window manager's overlay. Allocated while the menu is shown. */
struct MenuState {
    Kind kind = Kind::Settings;
    lv_display_t* display = nullptr;
    lv_obj_t* card = nullptr;
    /** The close button and the power or clear button, which the arrow keys move between */
    lv_obj_t* topRow = nullptr;
    /** Removes all notifications, which is hidden while there are none */
    lv_obj_t* clearButton = nullptr;
    /** Pushes the clear button to the right, which is hidden when the card is a single row */
    lv_obj_t* clearGap = nullptr;
    /** The quick settings, which are null while the notifications are shown */
    lv_obj_t* quickPanel = nullptr;
    /** The standby, suspend and power off options, which are hidden until the power button is checked. Null without options. */
    lv_obj_t* powerPanel = nullptr;
    lv_obj_t* powerButton = nullptr;
    /** The memory indicators, which are null on devices where free memory is not a concern */
    lv_obj_t* memoryRow = nullptr;
    lv_timer_t* memoryTimer = nullptr;
    struct MemoryGauge {
        lv_obj_t* arc = nullptr;
        lv_obj_t* inner = nullptr;
        lv_obj_t* value = nullptr;
        lv_obj_t* unit = nullptr;
    } ramGauge, psramGauge;
    /** The tiles of the quick settings, which are above the level controls */
    lv_obj_t* tileGrid = nullptr;
    /** Holds the level controls, which is null without sound output and without a backlight that can be dimmed */
    lv_obj_t* controls = nullptr;
    /** The speaker volume, with a root that is null without sound output */
    LevelControl volume;
    lv_obj_t* volumeMute = nullptr;
    lv_obj_t* volumeMuteIcon = nullptr;
    /** The brightness of the display, with a root that is null when the backlight can't be dimmed */
    LevelControl brightness;
    NotificationPanel* notifications = nullptr;
    size_t notificationTotal = 0;
};

static MenuState* menu = nullptr;

// Only changed and read with the LVGL lock held
static bool notificationsOpen = false;
static uint32_t notificationsChangedOrder = 0;

bool systemMenuNotificationsSeenSince(uint32_t eventOrder) {
    return notificationsOpen || notificationsChangedOrder >= eventOrder;
}

static void setNotificationsOpen(bool open) {
    notificationsOpen = open;
    // Events up to this one happened before the change, and later ones after it
    notificationsChangedOrder = notification_get_event_order();
}

// Internal RAM is judged by what is left, as a share of a small heap says little
constexpr size_t RAM_WARNING_BYTES = 64 * 1024;
constexpr size_t RAM_CRITICAL_BYTES = 32 * 1024;
constexpr int PSRAM_WARNING_PERCENT = 20;
constexpr int PSRAM_CRITICAL_PERCENT = 10;

enum class MemoryLevel { Normal, Warning, Critical };

/** Formats the size with at most 3 digits, in the unit that makes that possible: kB, MB or GB, counted in steps of 1024 */
static void formatMemory(size_t bytes, char* text, size_t textSize, const char*& unit) {
    static constexpr const char* UNITS[] = { "kB", "MB", "GB" };
    double value = static_cast<double>(bytes) / 1024.0;
    int unitIndex = 0;
    while (value >= 999.5 && unitIndex < 2) {
        value /= 1024.0;
        unitIndex++;
    }
    if (value < 9.95) {
        snprintf(text, textSize, "%.1f", value);
    } else {
        snprintf(text, textSize, "%.0f", value);
    }
    unit = UNITS[unitIndex];
}

static void updateGauge(const MenuState::MemoryGauge& gauge, size_t freeBytes, size_t totalBytes, MemoryLevel level) {
    const int percent = totalBytes > 0 ? static_cast<int>(static_cast<uint64_t>(freeBytes) * 100 / totalBytes) : 0;
    lv_arc_set_value(gauge.arc, percent);

    char text[12];
    const char* unit = "kB";
    formatMemory(freeBytes, text, sizeof(text), unit);
    lv_label_set_text(gauge.value, text);
    lv_label_set_text(gauge.unit, unit);

    LvglThemeSettings theme {};
    lvgl_theme_get_settings(&theme);
    const lv_color_t text_color = lv_obj_get_style_text_color(gauge.arc, LV_PART_MAIN);
    lv_color_t color = theme.color_secondary;
    if (level == MemoryLevel::Warning) {
        color = lv_palette_main(LV_PALETTE_AMBER);
    } else if (level == MemoryLevel::Critical) {
        color = theme.color_error;
    }
    // The unfilled part is the background of the card, a little towards the text color
    lv_obj_set_style_arc_color(gauge.arc, lv_color_mix(text_color, lv_obj_get_style_bg_color(menu->card, LV_PART_MAIN), 40), LV_PART_MAIN);
    if (lvgl_theme_is_mono()) {
        // No colors, so a critical gauge inverts: a solid disc with the value in the background color
        const bool critical = level == MemoryLevel::Critical;
        lv_obj_set_style_arc_color(gauge.arc, text_color, LV_PART_INDICATOR);
        // Only the ring marks the unfilled part, as a gray can't be shown
        lv_obj_set_style_arc_color(gauge.arc, lv_obj_get_style_bg_color(menu->card, LV_PART_MAIN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(gauge.inner, critical ? LV_OPA_COVER : LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(gauge.inner, text_color, LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(gauge.arc, text_color, LV_PART_MAIN);
        lv_obj_set_style_text_color(gauge.value, critical ? lv_obj_get_style_bg_color(menu->card, LV_PART_MAIN) : text_color, LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_arc_color(gauge.arc, color, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(gauge.value, level == MemoryLevel::Normal ? text_color : color, LV_STATE_DEFAULT);
    }
}

static void updateMemory() {
    if (menu->memoryRow == nullptr) {
        return;
    }
    const size_t ramFree = memory_heap_free();
    MemoryLevel ramLevel = MemoryLevel::Normal;
    if (ramFree < RAM_CRITICAL_BYTES) {
        ramLevel = MemoryLevel::Critical;
    } else if (ramFree < RAM_WARNING_BYTES) {
        ramLevel = MemoryLevel::Warning;
    }
    updateGauge(menu->ramGauge, ramFree, memory_heap_total(), ramLevel);

    const size_t psramTotal = memory_external_total();
    if (psramTotal > 0) {
        const size_t psramFree = memory_external_free();
        const int percent = static_cast<int>(static_cast<uint64_t>(psramFree) * 100 / psramTotal);
        MemoryLevel psramLevel = MemoryLevel::Normal;
        if (percent < PSRAM_CRITICAL_PERCENT) {
            psramLevel = MemoryLevel::Critical;
        } else if (percent < PSRAM_WARNING_PERCENT) {
            psramLevel = MemoryLevel::Warning;
        }
        updateGauge(menu->psramGauge, psramFree, psramTotal, psramLevel);
    }
}

static void onMemoryTimer(lv_timer_t* /*timer*/) {
    if (menu != nullptr) {
        updateMemory();
    }
}

/**
 * Shows the memory indicators only when the top row has room for them next to its buttons.
 * When there is only room for one, internal RAM is shown. Their row stays, so the power button remains on the right.
 */
static void updateMemoryVisibility() {
    if (menu->memoryRow == nullptr) {
        return;
    }
    const uint32_t count = lv_obj_get_child_count(menu->memoryRow);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_set_hidden(lv_obj_get_child(menu->memoryRow, static_cast<int32_t>(i)), false);
    }
    lv_obj_update_layout(menu->topRow);
    int32_t needed = 0;
    for (uint32_t i = 0; i < count; i++) {
        needed += lv_obj_get_width(lv_obj_get_child(menu->memoryRow, static_cast<int32_t>(i)));
    }
    if (count > 1) {
        needed += static_cast<int32_t>(count - 1) * lv_obj_get_style_pad_column(menu->memoryRow, LV_PART_MAIN);
    }
    const int32_t available = lv_obj_get_content_width(menu->memoryRow);
    // When they don't all fit, the first one (internal RAM) stays if it fits alone
    const bool fitsAll = needed <= available;
    const bool fitsFirst = lv_obj_get_width(lv_obj_get_child(menu->memoryRow, 0)) <= available;
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_set_hidden(lv_obj_get_child(menu->memoryRow, static_cast<int32_t>(i)), !(fitsAll || (i == 0 && fitsFirst)));
    }
}

static MenuState::MemoryGauge createGauge(lv_obj_t* parent, const char* name, bool captionLeft) {
    MenuState::MemoryGauge gauge;
    const lv_font_t* font = lvgl_get_text_font(FONT_SIZE_SMALL);
    const int32_t size = lv_font_get_line_height(font) * 2 + LV_DPX(10);

    auto* item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    // Reversed, the caption is left of the ring
    lv_obj_set_flex_flow(item, captionLeft ? LV_FLEX_FLOW_ROW_REVERSE : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, LV_DPX(6), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(item, false);

    gauge.arc = lv_arc_create(item);
    lv_obj_set_size(gauge.arc, size, size);
    lv_arc_set_rotation(gauge.arc, 270);
    lv_arc_set_bg_angles(gauge.arc, 0, 360);
    lv_arc_set_range(gauge.arc, 0, 100);
    lv_obj_remove_style(gauge.arc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(gauge.arc, LV_DPX(5), LV_PART_MAIN);
    lv_obj_set_style_arc_width(gauge.arc, LV_DPX(5), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(gauge.arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(gauge.arc, 0, LV_STATE_DEFAULT);
    if (lvgl_theme_is_mono()) {
        // A thin ring over the outer edge of the arc, drawn after it
        lv_obj_set_style_bg_opa(gauge.arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_radius(gauge.arc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(gauge.arc, LV_DPX(1), LV_PART_MAIN);
        lv_obj_set_style_border_opa(gauge.arc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_post(gauge.arc, true, LV_PART_MAIN);
    }
    lv_obj_set_clickable(gauge.arc, false);

    gauge.inner = lv_obj_create(gauge.arc);
    lv_obj_remove_style_all(gauge.inner);
    lv_obj_set_size(gauge.inner, size - 2 * LV_DPX(6), size - 2 * LV_DPX(6));
    lv_obj_set_style_radius(gauge.inner, LV_RADIUS_CIRCLE, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(gauge.inner, false);
    lv_obj_set_clickable(gauge.inner, false);
    lv_obj_center(gauge.inner);

    gauge.value = lv_label_create(gauge.inner);
    lv_obj_set_style_text_font(gauge.value, font, LV_STATE_DEFAULT);
    lv_label_set_long_mode(gauge.value, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_center(gauge.value);

    // The name above the unit, which changes with the amount
    auto* captions = lv_obj_create(item);
    lv_obj_remove_style_all(captions);
    lv_obj_set_size(captions, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(captions, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(captions, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(captions, false);
    if (captionLeft) {
        lv_obj_set_style_text_align(captions, LV_TEXT_ALIGN_RIGHT, LV_STATE_DEFAULT);
        lv_obj_set_flex_align(captions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    }
    auto* nameLabel = lv_label_create(captions);
    lv_obj_set_style_text_font(nameLabel, font, LV_STATE_DEFAULT);
    lv_label_set_text(nameLabel, name);
    gauge.unit = lv_label_create(captions);
    lv_obj_set_style_text_font(gauge.unit, font, LV_STATE_DEFAULT);
    // The width of the widest unit, so the row doesn't shift when the unit changes
    lv_obj_set_width(gauge.unit, lv_font_get_glyph_width(font, 'M', 0) * 2 + lv_font_get_glyph_width(font, 'B', 0));
    return gauge;
}

/** The indicators only show values, so the keys and touch must pass them by */
static void makeInert(lv_obj_t* obj) {
    lv_obj_set_clickable(obj, false);
    if (lv_obj_get_group(obj) != nullptr) {
        lv_group_remove_obj(obj);
    }
    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; i++) {
        makeInert(lv_obj_get_child(obj, static_cast<int32_t>(i)));
    }
}

/** Creates "[ring] RAM   [ring] PSRAM", where the rings fill with the free memory. PSRAM is left out when there is none. */
static void createMemoryRow(lv_obj_t* parent) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    // Takes the width that the buttons leave, with the rings in the middle of it
    lv_obj_set_size(row, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, lvgl_theme_lerp(12, 24, lvgl_theme_get_spacing()), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(row, LV_DPX(2), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(row, false);
    menu->memoryRow = row;

    // With both rings, the caption of RAM is on its left so the two rings are next to each other
    const bool hasPsram = memory_external_total() > 0;
    menu->ramGauge = createGauge(row, "RAM", hasPsram);
    if (hasPsram) {
        menu->psramGauge = createGauge(row, "PSRAM", false);
    }
    makeInert(row);
    updateMemory();
    menu->memoryTimer = lv_timer_create(onMemoryTimer, 2000, nullptr);
}

static constexpr int32_t VOLUME_STEP = 10;
/** The widest a level control gets when it is alone, so that its slider stays easy to use on a wide card */
static constexpr int32_t SINGLE_CONTROL_MAX_WIDTH_DPX = 360;

static void setLevel(LevelControl* control, int32_t level) {
    lv_slider_set_value(control->slider, level, LV_ANIM_OFF);
    lv_label_set_text_fmt(control->label, "%d %%", static_cast<int>(level));
    control->apply(level);
}

static void onLevelSliderChanged(lv_event_t* event) {
    auto* control = static_cast<LevelControl*>(lv_event_get_user_data(event));
    setLevel(control, lv_slider_get_value(control->slider));
}

static void onLevelSliderReleased(lv_event_t* event) {
    auto* control = static_cast<LevelControl*>(lv_event_get_user_data(event));
    if (control->commit != nullptr) {
        control->commit();
    }
}

static void onLevelStep(lv_event_t* event) {
    auto* argument = static_cast<LevelControl::StepArgument*>(lv_event_get_user_data(event));
    auto* control = argument->control;
    setLevel(control, LV_CLAMP(control->min, lv_slider_get_value(control->slider) + argument->direction * control->step, 100));
    if (control->commit != nullptr) {
        control->commit();
    }
}

static void applyVolume(int32_t level) {
    tt::service::audio::setOutputVolume(static_cast<float>(level));
}

static void applyBrightness(int32_t level) {
    auto* backlight = settings::display::getBacklight();
    if (backlight == nullptr) {
        return;
    }
    const auto duty = static_cast<uint8_t>(settings::display::backlightPercentToBrightness(backlight, level));
    backlight_set_brightness(backlight, duty);
    if (!settings::display::queueSaveBacklightDuty(duty)) {
        LOG_E(TAG, "Failed to queue saving the brightness");
    }
    device_put(backlight);
}

// Checked while the sound is on, like the other toggles
static void updateVolumeMuteToggle() {
    const bool on = !tt::service::audio::isOutputMuted();
    lv_obj_set_state(menu->volumeMute, LV_STATE_CHECKED, on);
    lv_label_set_text(menu->volumeMuteIcon, on ? LVGL_ICON_SHARED_VOLUME_UP : LVGL_ICON_SHARED_VOLUME_OFF);
}

static void onVolumeMuteChanged(lv_event_t* /*event*/) {
    tt::service::audio::setOutputMuted(!lv_obj_has_state(menu->volumeMute, LV_STATE_CHECKED));
    updateVolumeMuteToggle();
}

static int32_t getStepButtonSize() {
    return lvgl_get_text_font_height(FONT_SIZE_DEFAULT) + 8;
}

static void createStepButton(lv_obj_t* parent, const char* symbol, LevelControl::StepArgument* argument) {
    auto* button = lv_button_create(parent);
    const auto size = getStepButtonSize();
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_pad_all(button, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(button, onLevelStep, LV_EVENT_SHORT_CLICKED, argument);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
}

static lv_obj_t* createLevelRow(lv_obj_t* parent, LevelControl& control) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_style_pad_column(row, LV_DPX(12), LV_STATE_DEFAULT);
    control.root = row;
    return row;
}

/** Adds "[-] [slider and percentage] [+]" after the icon that is in the row */
static void addLevelControls(LevelControl& control, int32_t level) {
    auto* row = control.root;
    control.minus = { &control, -1 };
    control.plus = { &control, 1 };
    createStepButton(row, LV_SYMBOL_MINUS, &control.minus);

    control.slider = lv_slider_create(row);
    lv_obj_set_flex_grow(control.slider, 1);
    lv_slider_set_range(control.slider, control.min, 100);
    lv_obj_add_event_cb(control.slider, onLevelSliderChanged, LV_EVENT_VALUE_CHANGED, &control);
    lv_obj_add_event_cb(control.slider, onLevelSliderReleased, LV_EVENT_RELEASED, &control);

    // The width of the widest text, so that the plus button stays in place for 1, 2 and 3 digits
    control.label = lv_label_create(row);
    lv_obj_set_style_text_align(control.label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_long_mode(control.label, LV_LABEL_LONG_MODE_CLIP);
    const lv_font_t* font = lv_obj_get_style_text_font(control.label, LV_PART_MAIN);
    const int32_t width = 3 * lv_font_get_glyph_width(font, '0', 0) + lv_font_get_glyph_width(font, ' ', 0) + lv_font_get_glyph_width(font, '%', 0);
    control.labelWidth = width + LV_DPX(4);
    lv_obj_set_width(control.label, control.labelWidth);
    lv_obj_set_hidden(control.label, true);

    createStepButton(row, LV_SYMBOL_PLUS, &control.plus);

    lv_slider_set_value(control.slider, level, LV_ANIM_OFF);
    lv_label_set_text_fmt(control.label, "%d %%", static_cast<int>(level));
    // The left and right keys move between the buttons and the slider
    lvgl_grid_navigation_add(row);
}

/** Creates "[speaker] [-] [slider] [+]" */
static void createVolumeControl(lv_obj_t* parent) {
    menu->volume.step = VOLUME_STEP;
    menu->volume.apply = applyVolume;
    auto* row = createLevelRow(parent, menu->volume);

    menu->volumeMute = createTile(row, LVGL_ICON_SHARED_VOLUME_UP);
    menu->volumeMuteIcon = lv_obj_get_child(menu->volumeMute, 0);
    lv_obj_set_checkable(menu->volumeMute, true);
    lv_obj_add_event_cb(menu->volumeMute, onVolumeMuteChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    updateVolumeMuteToggle();

    addLevelControls(menu->volume, static_cast<int32_t>(tt::service::audio::getOutputVolume()));
}

/** Creates "[display] [-] [slider] [+]", where the icon only shows what the control is for */
static void createBrightnessControl(lv_obj_t* parent, int32_t level) {
    menu->brightness.min = settings::display::BACKLIGHT_MIN_PERCENT;
    menu->brightness.step = settings::display::BACKLIGHT_STEP_PERCENT;
    menu->brightness.apply = applyBrightness;
    auto* row = createLevelRow(parent, menu->brightness);

    const int32_t size = getTileSize();
    auto* icon = lv_obj_create(row);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, size, size);
    lv_obj_set_scrollable(icon, false);
    lv_obj_set_clickable(icon, false);
    auto* icon_label = lv_label_create(icon);
    lv_obj_set_style_text_font(icon_label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(icon_label, LVGL_ICON_SHARED_DISPLAY_SETTINGS);
    lv_obj_center(icon_label);

    addLevelControls(menu->brightness, level);
}

static void showLevelControl(LevelControl& control, bool show, bool useSlider) {
    lv_obj_set_hidden(control.root, !show);
    lv_obj_set_hidden(control.slider, !useSlider);
    lv_obj_set_hidden(control.label, useSlider);
}

/** The width of a control: the icon, the 2 buttons and the slider or the percentage, with the gaps between them */
static int32_t getLevelControlWidth(const LevelControl& control, bool useSlider) {
    return getTileSize() + 2 * getStepButtonSize() + (useSlider ? LV_DPX(80) : control.labelWidth) + 3 * LV_DPX(12);
}

/** Places a control: it either takes the full width of the container (up to a maximum) or a share of its row */
static void placeLevelControl(LevelControl& control, bool inRow, bool single) {
    lv_obj_set_width(control.root, inRow ? 0 : LV_PCT(100));
    lv_obj_set_flex_grow(control.root, inRow ? 1 : 0);
    if (single) {
        lv_obj_set_style_max_width(control.root, LV_DPX(SINGLE_CONTROL_MAX_WIDTH_DPX), LV_STATE_DEFAULT);
    } else {
        lv_obj_remove_local_style_prop(control.root, LV_STYLE_MAX_WIDTH, LV_PART_MAIN);
    }
}

/**
 * Picks the most that fits: both below each other, with sliders when they fit. Otherwise only the volume
 * (or the brightness without sound), with a slider when it fits, and centered.
 * A slider needs a pointer to use and enough width to be grabbed.
 */
static void updateControls() {
    if (menu->controls == nullptr || lv_obj_is_hidden(menu->quickPanel)) {
        return;
    }
    lv_obj_update_layout(menu->quickPanel);
    const bool hasVolume = menu->volume.root != nullptr;
    const bool hasBrightness = menu->brightness.root != nullptr;
    LevelControl* first = hasVolume ? &menu->volume : &menu->brightness;
    const bool pointer = lvgl_indev_exists(LV_INDEV_TYPE_POINTER);
    const int32_t width = lv_obj_get_content_width(menu->controls);
    const int32_t gap = lv_obj_get_style_pad_row(menu->controls, LV_PART_MAIN);
    const int32_t rowHeight = std::max(getTileSize(), getStepButtonSize());
    // What the tiles leave of the panel
    const int32_t room = lv_obj_get_height(menu->quickPanel) - lv_obj_get_height(menu->tileGrid) - lv_obj_get_style_pad_row(menu->quickPanel, LV_PART_MAIN);
    const bool sliderFits = pointer && width >= getLevelControlWidth(*first, true);

    const bool both = hasVolume && hasBrightness;
    lv_obj_set_flex_flow(menu->controls, LV_FLEX_FLOW_COLUMN);
    if (both && room >= 2 * rowHeight + gap) {
        placeLevelControl(menu->volume, false, false);
        placeLevelControl(menu->brightness, false, false);
        showLevelControl(menu->volume, true, sliderFits);
        showLevelControl(menu->brightness, true, sliderFits);
    } else {
        placeLevelControl(*first, false, true);
        showLevelControl(*first, true, sliderFits);
        if (both) {
            showLevelControl(menu->brightness, false, false);
        }
    }
}

/**
 * The card is as high as the notifications need. With at most one, it is a single row of the buttons and the
 * notification or the empty text. Otherwise the list is below the buttons.
 */
static void layoutNotifications(int32_t maxCardHeight) {
    if (menu->notifications == nullptr) {
        return;
    }
    const bool single = menu->notificationTotal <= 1;
    auto* root = notificationPanelGetRoot(menu->notifications);
    lv_obj_set_flex_flow(menu->card, single ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_column(menu->card, 0, LV_STATE_DEFAULT);
    lv_obj_set_hidden(menu->clearGap, single);
    lv_obj_set_width(menu->topRow, single ? LV_SIZE_CONTENT : LV_PCT(100));
    lv_obj_set_width(root, single ? 0 : LV_PCT(100));
    lv_obj_set_flex_grow(root, single ? 1 : 0);
    lv_obj_update_layout(menu->card);
    int32_t available = maxCardHeight - lv_obj_get_style_pad_top(menu->card, LV_PART_MAIN) - lv_obj_get_style_pad_bottom(menu->card, LV_PART_MAIN);
    if (!single) {
        available -= lv_obj_get_height(menu->topRow) + lv_obj_get_style_pad_row(menu->card, LV_PART_MAIN);
    }
    notificationPanelFit(menu->notifications, available);
}

/** The card fills most of a narrow display, and is narrower on a wide one where the content would stretch */
static void applyCardSize() {
    const int32_t width = lv_display_get_horizontal_resolution(menu->display);
    const int32_t height = lv_display_get_vertical_resolution(menu->display);
    const bool wide = width > height && width >= WIDE_DISPLAY_MIN_WIDTH;
    if (menu->kind == Kind::Notifications) {
        const int32_t maxHeight = height * (wide ? 80 : 90) / 100;
        lv_obj_set_size(menu->card, lv_pct(wide ? 60 : 90), LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(menu->card, maxHeight, LV_STATE_DEFAULT);
        layoutNotifications(maxHeight);
        return;
    }
    lv_obj_set_size(menu->card, lv_pct(wide ? 60 : 90), lv_pct(wide ? 80 : 90));
    if (menu->quickPanel != nullptr) {
        lv_obj_update_layout(menu->card);
        updateControls();
        updateMemoryVisibility();
    }
}

static void onDisplayResolutionChanged(lv_event_t* /*event*/) {
    if (menu != nullptr) {
        applyCardSize();
    }
}

/** Creates the volume and brightness controls, for what the device has. They are arranged when the card is sized. */
static void createControls(lv_obj_t* parent) {
    const bool hasVolume = tt::service::audio::isOutputAvailable();
    auto* backlight = settings::display::getBacklight();
    const bool hasBrightness = backlight != nullptr && settings::display::backlightSupportsBrightness(backlight);
    int32_t brightnessLevel = 100;
    if (hasBrightness) {
        uint8_t duty = 0;
        if (backlight_get_brightness(backlight, &duty) == ERROR_NONE) {
            brightnessLevel = settings::display::backlightBrightnessToPercent(backlight, duty);
        }
    }
    if (backlight != nullptr) {
        device_put(backlight);
    }
    if (!hasVolume && !hasBrightness) {
        return;
    }

    auto* controls = lv_obj_create(parent);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(controls, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(controls, LV_DPX(12), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(controls, false);
    menu->controls = controls;

    if (hasVolume) {
        createVolumeControl(controls);
    }
    if (hasBrightness) {
        createBrightnessControl(controls, brightnessLevel);
    }
}

static lv_obj_t* createQuickSettingsPanel(lv_obj_t* parent) {
    auto* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_style_pad_row(panel, 4, LV_STATE_DEFAULT);

    // The tiles are centered in the space that the memory and the volume row leave. Spacers that grow instead of
    // a centered alignment keep the first tile reachable by scrolling when the tiles don't fit.
    auto* scrollArea = lv_obj_create(panel);
    lv_obj_remove_style_all(scrollArea);
    lv_obj_set_width(scrollArea, LV_PCT(100));
    lv_obj_set_height(scrollArea, 0);
    lv_obj_set_flex_grow(scrollArea, 1);
    lv_obj_set_flex_flow(scrollArea, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(scrollArea, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scrollArea, LV_SCROLLBAR_MODE_AUTO);

    auto* spacerTop = lv_obj_create(scrollArea);
    lv_obj_remove_style_all(spacerTop);
    lv_obj_set_size(spacerTop, 0, 0);
    lv_obj_set_flex_grow(spacerTop, 1);

    auto* grid = lv_obj_create(scrollArea);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const int32_t gap = lvgl_theme_lerp(8, 16, lvgl_theme_get_spacing());
    lv_obj_set_style_pad_column(grid, gap, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(grid, gap, LV_STATE_DEFAULT);
    menu->tileGrid = grid;

    static constexpr ToggleOption wifi_option = { LVGL_ICON_SHARED_WIFI, LVGL_ICON_SHARED_WIFI_OFF, isWifiEnabled, setWifiEnabled, "tactility.wifimanage" };
    if (hasWifi()) {
        createOption(grid, &wifi_option);
    }
    static constexpr ToggleOption location_option = { LVGL_ICON_SHARED_LOCATION_ON, LVGL_ICON_SHARED_LOCATION_OFF, isLocationEnabled, setLocationEnabled, "tactility.locationsettings" };
    createOption(grid, &location_option);
    static constexpr ToggleOption bluetooth_option = { LVGL_ICON_SHARED_BLUETOOTH, LVGL_ICON_SHARED_BLUETOOTH_DISABLED, isBluetoothEnabled, setBluetoothEnabled, "tactility.btmanage" };
    if (hasBluetooth()) {
        createOption(grid, &bluetooth_option);
    }
    // There is no "off" variant of this icon: the toggle's checked state shows whether mesh is on
    static constexpr ToggleOption mesh_option = { LVGL_ICON_SHARED_HUB, LVGL_ICON_SHARED_HUB, isMeshEnabled, setMeshEnabled, "tactility.meshsettings" };
    if (hasMesh()) {
        createOption(grid, &mesh_option);
    }
    // The arrow keys move between the tiles. Toggles that are focused directly would be switched by them.
    lvgl_grid_navigation_add(grid);

    auto* spacerBottom = lv_obj_create(scrollArea);
    lv_obj_remove_style_all(spacerBottom);
    lv_obj_set_size(spacerBottom, 0, 0);
    lv_obj_set_flex_grow(spacerBottom, 1);

    // The level controls stay at the bottom of the panel
    createControls(panel);
    return panel;
}

static void onNotificationCount(void* /*userData*/, size_t total, size_t clearable) {
    // Nothing that can be cleared. A hidden button also leaves the focus order.
    lv_obj_set_hidden(menu->clearButton, clearable == 0);
    menu->notificationTotal = total;
    // The list is rebuilt while the menu is created and the card isn't sized yet. It is laid out when it is.
    if (menu->notifications != nullptr) {
        applyCardSize();
    }
}

/** Removes the notifications that aren't sticky, as the sender decides when a sticky one goes */
static void onClearClicked(lv_event_t* /*event*/) {
    notification_dismiss_non_sticky();
}

static void createGap(lv_obj_t* parent, int32_t width) {
    auto* gap = lv_obj_create(parent);
    lv_obj_remove_style_all(gap);
    lv_obj_set_size(gap, width, 1);
    lv_obj_set_clickable(gap, false);
}

static void onPowerToggled(lv_event_t* /*event*/) {
    if (!systemMenuPowerToggle(menu->powerButton, menu->quickPanel, menu->powerPanel)) {
        // The display can have rotated while the quick settings were hidden
        lv_obj_update_layout(menu->card);
        updateControls();
    }
}

/** Creates the close button, the memory indicators and the power button in one row that the arrow keys navigate */
static lv_obj_t* createTopRow(lv_obj_t* parent) {
    auto* topRow = lv_obj_create(parent);
    lv_obj_remove_style_all(topRow);
    lv_obj_set_size(topRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(topRow, false);
    lv_obj_set_style_pad_column(topRow, LV_DPX(8), LV_STATE_DEFAULT);
    menu->topRow = topRow;

    // A keyboard or encoder can't tap outside of the card or swipe up, so it needs a button to close the menu
    lv_obj_t* closeButton = nullptr;
    if (lvgl_indev_exists(LV_INDEV_TYPE_KEYPAD) || lvgl_indev_exists(LV_INDEV_TYPE_ENCODER)) {
        closeButton = createTile(topRow, LVGL_ICON_SHARED_CLOSE);
        lv_obj_add_event_cb(closeButton, onCloseClicked, LV_EVENT_SHORT_CLICKED, nullptr);
    }

    // Between the buttons, with a gap as wide as a button where one is missing, so the rings are in the middle of the row
    const bool hasPower = menu->kind == Kind::Settings && systemMenuHasPowerOptions();
    if (menu->kind == Kind::Settings) {
        const auto tileSize = getTileSize();
        if (closeButton == nullptr && hasPower) {
            createGap(topRow, tileSize);
        }
        createMemoryRow(topRow);
        if (closeButton != nullptr && !hasPower) {
            createGap(topRow, tileSize);
        }
    }

    if (hasPower) {
        menu->powerButton = createTile(topRow, LVGL_ICON_SHARED_POWER_SETTINGS_NEW);
        // Checked while the power options are shown
        lv_obj_set_checkable(menu->powerButton, true);
        lv_obj_add_event_cb(menu->powerButton, onPowerToggled, LV_EVENT_VALUE_CHANGED, nullptr);
    }
    if (menu->kind == Kind::Notifications) {
        // A growing gap pushes the button to the right
        auto* gap = lv_obj_create(topRow);
        menu->clearGap = gap;
        lv_obj_remove_style_all(gap);
        lv_obj_set_size(gap, 0, 1);
        lv_obj_set_flex_grow(gap, 1);
        lv_obj_set_clickable(gap, false);
        menu->clearButton = lvgl_text_button_create(topRow);
        lv_obj_add_event_cb(menu->clearButton, onClearClicked, LV_EVENT_SHORT_CLICKED, nullptr);
        lv_label_set_text(lv_label_create(menu->clearButton), tr("Clear"));
    }
    // The left and right keys move between the buttons
    lvgl_grid_navigation_add(topRow);
    return closeButton;
}

static void destroyWidgets(void* /*userData*/) {
    if (menu == nullptr) {
        return;
    }
    lv_display_remove_event_cb_with_user_data(menu->display, onDisplayResolutionChanged, nullptr);
    if (menu->notifications != nullptr) {
        notificationPanelDestroy(menu->notifications);
    }
    if (menu->memoryTimer != nullptr) {
        lv_timer_delete(menu->memoryTimer);
    }
    // Only a list that was shown counts as seen
    if (menu->notifications != nullptr) {
        setNotificationsOpen(false);
    }
    tt::deleteObject(menu);
    menu = nullptr;
}

static void createWidgets(lv_obj_t* parent, void* userData) {
    // Swiping up anywhere closes the menu
    lv_obj_set_gesture_bubble(parent, false);
    lv_obj_add_event_cb(parent, onBackdropGesture, LV_EVENT_GESTURE, nullptr);

    menu = tt::newObject<MenuState>();
    if (menu == nullptr) {
        LOG_E(TAG, "Out of memory");
        return;
    }
    menu->display = lv_obj_get_display(parent);
    const auto data = reinterpret_cast<intptr_t>(userData);
    menu->kind = static_cast<Kind>(data & KIND_MASK);
    const bool showPowerOptions = (data & SHOW_POWER_OPTIONS) != 0;

    menu->card = lvgl_card_create(parent);
    lv_obj_set_flex_flow(menu->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(menu->card, false);
    lv_obj_set_flex_align(menu->card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(menu->card);

    lv_obj_t* close_button = createTopRow(menu->card);
    if (menu->kind == Kind::Settings) {
        menu->quickPanel = createQuickSettingsPanel(menu->card);
        if (menu->powerButton != nullptr) {
            menu->powerPanel = systemMenuPowerPanelCreate(menu->card);
            lv_obj_set_state(menu->powerButton, LV_STATE_CHECKED, showPowerOptions);
            systemMenuPowerToggle(menu->powerButton, menu->quickPanel, menu->powerPanel);
        }
    } else {
        menu->notifications = notificationPanelCreate(menu->card, onNotificationCount, nullptr);
        if (menu->notifications == nullptr) {
            LOG_E(TAG, "Failed to create the notification panel");
            return;
        }
        // The user is looking at them now, which is only true once the list exists
        setNotificationsOpen(true);
        system_bars_set_notification_unread(false);
    }

    if (close_button != nullptr) {
        // The row is in the focus group, and it focuses the button for the keys. The power button when it opened the power options.
        lv_obj_t* focused_button = (showPowerOptions && menu->powerButton != nullptr) ? menu->powerButton : close_button;
        lv_group_focus_obj(menu->topRow);
        lv_gridnav_set_focused(menu->topRow, focused_button, LV_ANIM_OFF);
        lv_obj_add_state(focused_button, LV_STATE_FOCUS_KEY);
    }

    // The display can rotate while the menu is open
    lv_display_add_event_cb(menu->display, onDisplayResolutionChanged, LV_EVENT_RESOLUTION_CHANGED, nullptr);
    applyCardSize();
}

static void showMenu(Kind kind, bool showPowerOptions = false) {
    const intptr_t data = static_cast<intptr_t>(kind) | (showPowerOptions ? SHOW_POWER_OPTIONS : 0);
    window_manager_overlay_show(createWidgets, destroyWidgets, reinterpret_cast<void*>(data));
}

void systemMenuTogglePower() {
    // The menu only exists while it is shown
    if (menu != nullptr) {
        window_manager_overlay_hide();
    } else {
        showMenu(Kind::Settings, true);
    }
}

void systemMenuShowNotifications() {
    showMenu(Kind::Notifications);
}

void systemMenuShow(int32_t swipeX) {
    // A swipe from the right half of the screen shows the notifications. Anything else shows the settings.
    lv_display_t* display = lv_display_get_default();
    const bool fromRight = swipeX >= 0 && display != nullptr && swipeX >= lv_display_get_horizontal_resolution(display) / 2;
    showMenu(fromRight ? Kind::Notifications : Kind::Settings);
}

}
