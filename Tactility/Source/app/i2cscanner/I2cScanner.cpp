#include <Tactility/app/i2cscanner/I2cHelpers.h>
#include <Tactility/app/i2cscanner/I2cScannerPrivate.h>
#include <Tactility/LogMessages.h>
#include <Tactility/Preferences.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/Timer.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <cassert>
#include <format>
#include <string>
#include <vector>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::i2cscanner {

extern const ::AppManifest manifest;

namespace {

constexpr auto* TAG = "I2cScanner";

constexpr auto* START_SCAN_TEXT = "Scan";
constexpr auto* STOP_SCAN_TEXT = "Stop scan";

struct Context {
    uint32_t appInstanceId;

    // Core
    RecursiveMutex mutex;
    std::unique_ptr<Timer> scanTimer = nullptr;
    // State
    ScanState scanState = ScanStateInitial;
    struct Device* portDevice = nullptr;
    std::vector<uint8_t> scannedAddresses;
    // Widgets
    lv_obj_t* scanButtonLabelWidget = nullptr;
    lv_obj_t* portDropdownWidget = nullptr;
    lv_obj_t* scanListWidget = nullptr;
};


#define PREFERENCES_BUS_INDEX_KEY "bus"

void setLastBusIndex(int32_t index) {
    auto prefs = Preferences("i2c_scanner");
    prefs.putInt32(PREFERENCES_BUS_INDEX_KEY, index);
}

int32_t getLastBusIndex() {
    auto prefs = Preferences("i2c_scanner");
    int32_t index = 0;
    prefs.optInt32(PREFERENCES_BUS_INDEX_KEY, index);
    return index;
}

bool getPort(Context* ctx, struct Device** outPort) {
    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        *outPort = ctx->portDevice;
        ctx->mutex.unlock();
        return true;
    } else {
        LOG_W(TAG, "Mutex acquisition timeout (%s)", "getPort");
        return false;
    }
}

bool addAddressToList(Context* ctx, uint8_t address) {
    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        ctx->scannedAddresses.push_back(address);
        ctx->mutex.unlock();
        return true;
    } else {
        LOG_W(TAG, "Mutex acquisition timeout (%s)", "addAddressToList");
        return false;
    }
}

bool shouldStopScanTimer(Context* ctx) {
    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        bool is_scanning = ctx->scanState == ScanStateScanning;
        ctx->mutex.unlock();
        return !is_scanning;
    } else {
        return true;
    }
}

void updateViews(Context* ctx) {
    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        if (ctx->scanState == ScanStateScanning) {
            lv_label_set_text(ctx->scanButtonLabelWidget, STOP_SCAN_TEXT);
            lv_obj_remove_flag(ctx->portDropdownWidget, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_label_set_text(ctx->scanButtonLabelWidget, START_SCAN_TEXT);
            lv_obj_add_flag(ctx->portDropdownWidget, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_clean(ctx->scanListWidget);
        if (ctx->scanState == ScanStateStopped) {
            lv_obj_remove_flag(ctx->scanListWidget, LV_OBJ_FLAG_HIDDEN);

            if (!ctx->scannedAddresses.empty()) {
                for (auto address: ctx->scannedAddresses) {
                    std::string address_text = getAddressText(address);
                    lv_list_add_text(ctx->scanListWidget, address_text.c_str());
                }
            } else {
                lv_list_add_text(ctx->scanListWidget, "No devices found");
            }
        } else {
            lv_obj_add_flag(ctx->scanListWidget, LV_OBJ_FLAG_HIDDEN);
        }

        ctx->mutex.unlock();
    } else {
        LOG_W(TAG, "Mutex acquisition timeout (%s)", "updateViews");
    }
}

void updateViewsSafely(Context* ctx) {
    lvgl_lock();
    updateViews(ctx);
    lvgl_unlock();
}

void onScanTimerFinished(Context* ctx) {
    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        if (ctx->scanState == ScanStateScanning) {
            ctx->scanState = ScanStateStopped;
        }
        ctx->mutex.unlock();

        updateViewsSafely(ctx);
    } else {
        LOG_W(TAG, "Mutex acquisition timeout (%s)", "onScanTimerFinished");
    }
}

void onScanTimer(Context* ctx) {
    LOG_I(TAG, "Scan thread started");

    Device* safe_port;
    if (!getPort(ctx, &safe_port)) {
        LOG_E(TAG, "Failed to get I2C port");
        onScanTimerFinished(ctx);
        return;
    }

    if (!device_is_ready(safe_port)) {
        LOG_E(TAG, "I2C port not started");
        onScanTimerFinished(ctx);
        return;
    }

    for (uint8_t address = 1; address < 128; ++address) {
        if (i2c_controller_has_device_at_address(safe_port, address, 10 / portTICK_PERIOD_MS) == ERROR_NONE) {
            LOG_I(TAG, "Found device at address 0x%02X", address);
            if (!shouldStopScanTimer(ctx)) {
                addAddressToList(ctx, address);
            } else {
                break;
            }
        }

        if (shouldStopScanTimer(ctx)) {
            break;
        }
    }

    LOG_I(TAG, "Scan thread finalizing");

    onScanTimerFinished(ctx);

    LOG_I(TAG, "Scan timer done");
}

bool hasScanThread(Context* ctx) {
    bool has_thread;
    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        has_thread = ctx->scanTimer != nullptr;
        ctx->mutex.unlock();
        return has_thread;
    } else {
        // Unsafe way
        LOG_W(TAG, "Mutex acquisition timeout (%s)", "hasScanTimer");
        return ctx->scanTimer != nullptr;
    }
}

void stopScanning(Context* ctx) {
    if (ctx->mutex.lock(250 / portTICK_PERIOD_MS)) {
        assert(ctx->scanTimer != nullptr);
        ctx->scanState = ScanStateStopped;
        ctx->mutex.unlock();
    } else {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
    }
}

void startScanning(Context* ctx) {
    if (hasScanThread(ctx)) {
        stopScanning(ctx);
    }

    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        ctx->scannedAddresses.clear();

        lv_obj_add_flag(ctx->scanListWidget, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clean(ctx->scanListWidget);

        ctx->scanState = ScanStateScanning;
        ctx->scanTimer = std::make_unique<Timer>(Timer::Type::Once, 10, [ctx]{
            onScanTimer(ctx);
        });
        ctx->scanTimer->start();
        ctx->mutex.unlock();
    } else {
        LOG_W(TAG, "Mutex acquisition timeout (%s)", "startScanning");
    }
}

void selectBus(Context* ctx, int32_t selected) {
    struct Device* found_device;
    if (!getActivePortAtIndex(selected, &found_device)) {
        return;
    }

    if (ctx->mutex.lock(100 / portTICK_PERIOD_MS)) {
        ctx->scannedAddresses.clear();
        ctx->portDevice = found_device;
        ctx->scanState = ScanStateInitial;
        ctx->mutex.unlock();
    }

    LOG_I(TAG, "Selected %d", (int)selected);
    setLastBusIndex(selected);

    startScanning(ctx);

    updateViews(ctx);
}

// region Callbacks

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onSelectBus(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
    uint32_t selected = lv_dropdown_get_selected(dropdown);
    selectBus(ctx, selected);
}

void onPressScan(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->scanState == ScanStateScanning) {
        stopScanning(ctx);
    } else {
        startScanning(ctx);
    }
    updateViews(ctx);
}

// endregion Callbacks

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "I2C Scanner");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    auto* wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_height(wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_set_style_border_width(wrapper, 0, 0);

    auto* scan_button = lv_button_create(wrapper);
    lv_obj_set_width(scan_button, LV_PCT(48));
    lv_obj_align(scan_button, LV_ALIGN_TOP_LEFT, 0, 1); // Shift 1 pixel to align with selection box
    lv_obj_add_event_cb(scan_button, onPressScan, LV_EVENT_SHORT_CLICKED, ctx);
    auto* scan_button_label = lv_label_create(scan_button);
    lv_obj_align(scan_button_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(scan_button_label, START_SCAN_TEXT);
    ctx->scanButtonLabelWidget = scan_button_label;

    auto* port_dropdown = lv_dropdown_create(wrapper);
    std::string dropdown_items = getPortNamesForDropdown();
    lv_dropdown_set_options(port_dropdown, dropdown_items.c_str());
    lv_obj_set_width(port_dropdown, LV_PCT(48));
    lv_obj_align(port_dropdown, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(port_dropdown, onSelectBus, LV_EVENT_VALUE_CHANGED, ctx);
    auto selected_bus = getLastBusIndex();
    lv_dropdown_set_selected(port_dropdown, selected_bus);
    ctx->portDropdownWidget = port_dropdown;

    auto* scan_list = lv_list_create(main_wrapper);
    lv_obj_set_style_margin_top(scan_list, 8, 0);
    lv_obj_set_width(scan_list, LV_PCT(100));
    lv_obj_set_height(scan_list, LV_SIZE_CONTENT);
    lv_obj_add_flag(scan_list, LV_OBJ_FLAG_HIDDEN);
    ctx->scanListWidget = scan_list;

    struct Device* dummy;
    if (getActivePortAtIndex(selected_bus, &dummy)) {
        selectBus(ctx, selected_bus);
    } else if (getActivePortAtIndex(0, &dummy)) {
        lv_dropdown_set_selected(port_dropdown, 0);
        selectBus(ctx, 0);
    }
}

// Mirrors the old model's onHide(): stop any in-flight scan before this app's task exits
// (APP_EVENT_CLOSE).
void stopScanningIfRunning(Context* ctx) {
    bool isRunning = false;
    if (ctx->mutex.lock(250 / portTICK_PERIOD_MS)) {
        auto* timer = ctx->scanTimer.get();
        if (timer != nullptr) {
            isRunning = timer->isRunning();
        }
        ctx->mutex.unlock();
    } else {
        return;
    }

    if (isRunning) {
        stopScanning(ctx);
    }
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx;
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
                stopScanningIfRunning(&ctx);
                app_manager_finish(appInstanceId);
                shouldClose = true;
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
    .id = "I2cScanner",
    .name = "I2C Scanner",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

uint32_t start() {
    uint32_t instanceId = 0;
    app_manager_start(manifest.id, &instanceId);
    return instanceId;
}

} // namespace
