
#include <Tactility/LogMessages.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/Timer.h>
#include <Tactility/app/i2cscanner/I2cHelpers.h>
#include <Tactility/app/i2cscanner/I2cScannerPrivate.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/paths.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>
#include <tactility/preferences.h>

#include <cassert>
#include <format>
#include <string>
#include <vector>

#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::i2cscanner {

extern const ::AppManifest manifest;

namespace {

constexpr auto* TAG = "I2cScanner";

struct Context {
    uint32_t appInstanceId;

    // Core
    RecursiveMutex mutex;
    std::unique_ptr<Timer> scanTimer = nullptr;
    // State
    ScanState scanState = ScanStateInitial;
    struct Device* portDevice = nullptr;
    int32_t selectedBus = 0;
    std::vector<uint8_t> scannedAddresses;
    /** Row and column sizes of the results grid, which must outlive the grid layout */
    std::vector<int32_t> gridRows;
    std::vector<int32_t> gridColumns;
    // Widgets: the chips only exist when there are I2C interfaces. Pressing a chip scans its bus.
    lv_obj_t* chipsRow = nullptr;
    lv_obj_t* scanListWidget = nullptr;
};


#define PREFERENCES_BUS_INDEX_KEY "bus"

bool getPreferencesPath(std::string& outPath) {
    char root[128];
    if (app_paths_get_user_data_directory(manifest.id, root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    outPath = std::string(root) + "/i2c_scanner.properties";
    return true;
}

void setLastBusIndex(int32_t index) {
    std::string path;
    if (!getPreferencesPath(path)) {
        return;
    }
    Preferences* prefs = preferences_open(path.c_str());
    if (prefs == nullptr) {
        return;
    }
    preferences_put_int32(prefs, PREFERENCES_BUS_INDEX_KEY, index);
    preferences_close(prefs);
}

int32_t getLastBusIndex() {
    std::string path;
    if (!getPreferencesPath(path)) {
        return 0;
    }
    Preferences* prefs = preferences_open(path.c_str());
    if (prefs == nullptr) {
        return 0;
    }
    int32_t index = 0;
    preferences_opt_int32(prefs, PREFERENCES_BUS_INDEX_KEY, &index);
    preferences_close(prefs);
    return index;
}

/** The names of the devicetree devices on the bus at the given address, separated by commas */
std::string getDeviceNames(struct Device* port, uint8_t address) {
    struct Search {
        uint8_t address;
        std::string names;
    } search = { address, "" };
    device_for_each_child(port, &search, [](struct Device* child, void* context) {
        auto* search = static_cast<Search*>(context);
        if (child->address == search->address) {
            if (!search->names.empty()) {
                search->names += ", ";
            }
            search->names += child->name;
        }
        return true;
    });
    return search.names;
}

// A disturbed probe can report a device that isn't there, while a real device acknowledges every probe
constexpr int PROBE_COUNT = 3;

bool hasDeviceAt(struct Device* port, uint8_t address) {
    for (int i = 0; i < PROBE_COUNT; i++) {
        if (i2c_controller_has_device_at_address(port, address, 10 / portTICK_PERIOD_MS) != ERROR_NONE) {
            return false;
        }
    }
    return true;
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
        if (ctx->scanListWidget == nullptr) {
            ctx->mutex.unlock();
            return;
        }

        // The scan reads the port once, so the bus can't change and the scan can't restart while it runs
        const bool scanning = ctx->scanState == ScanStateScanning;
        if (ctx->chipsRow != nullptr) {
            const uint32_t chip_count = lv_obj_get_child_count(ctx->chipsRow);
            for (uint32_t i = 0; i < chip_count; i++) {
                auto* chip = lv_obj_get_child(ctx->chipsRow, static_cast<int32_t>(i));
                lv_obj_set_state(chip, LV_STATE_CHECKED, static_cast<int32_t>(i) == ctx->selectedBus);
                lv_obj_set_state(chip, LV_STATE_DISABLED, scanning);
            }
        }

        lv_obj_clean(ctx->scanListWidget);
        if (ctx->scanState == ScanStateStopped) {
            lv_obj_set_hidden(ctx->scanListWidget, false);

            // A grid with the title in the first row, then an address and the devicetree device names per row
            const size_t count = ctx->scannedAddresses.size();
            ctx->gridColumns = { LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
            ctx->gridRows.assign(count + 1, LV_GRID_CONTENT);
            ctx->gridRows.push_back(LV_GRID_TEMPLATE_LAST);
            lv_obj_set_grid_dsc_array(ctx->scanListWidget, ctx->gridColumns.data(), ctx->gridRows.data());

            auto* title = lv_label_create(ctx->scanListWidget);
            lv_obj_set_grid_cell(title, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
            if (count == 0) {
                lv_label_set_text(title, "No devices found");
            } else {
                lv_label_set_text(title, std::format("{} {} found", count, count == 1 ? "device" : "devices").c_str());
            }
            for (size_t i = 0; i < count; i++) {
                const uint8_t address = ctx->scannedAddresses[i];
                const auto row = static_cast<int32_t>(i + 1);
                auto* address_label = lv_label_create(ctx->scanListWidget);
                lv_label_set_text(address_label, getAddressText(address).c_str());
                // Keys and encoders move through the addresses, which scrolls the results into view
                lv_obj_set_clickable(address_label, true);
                lv_obj_set_grid_cell(address_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, row, 1);

                auto* names_label = lv_label_create(ctx->scanListWidget);
                lv_label_set_long_mode(names_label, LV_LABEL_LONG_MODE_WRAP);
                lv_label_set_text(names_label, getDeviceNames(ctx->portDevice, address).c_str());
                lv_obj_set_grid_cell(names_label, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, row, 1);
            }
        } else {
            lv_obj_set_hidden(ctx->scanListWidget, true);
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
        if (hasDeviceAt(safe_port, address)) {
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

        lv_obj_set_hidden(ctx->scanListWidget, true);
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
        ctx->selectedBus = selected;
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
    app_event_emit_close(ctx->appInstanceId);
}

void onChipPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* chip = lv_event_get_target_obj(event);
    selectBus(ctx, static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(chip))));
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
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    const std::vector<std::string> port_names = getPortNames();
    if (port_names.empty()) {
        lv_obj_set_flex_align(main_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto* label = lv_label_create(main_wrapper);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_label_set_text(label, "I2C interface not available");
        return;
    }

    // Also shown for a single bus, as pressing a chip scans its bus again. Centered while the chips fit, and scrollable when they don't.
    lv_obj_set_flex_align(main_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    auto* chips_row = lv_obj_create(main_wrapper);
    lv_obj_set_size(chips_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(chips_row, LV_PCT(100), LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(chips_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(chips_row, LV_DIR_HOR);
    lv_obj_set_style_border_width(chips_row, 0, LV_STATE_DEFAULT);
    // The chips' margins leave room for their focus rings and space them apart
    lv_obj_set_style_pad_all(chips_row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(chips_row, 0, LV_STATE_DEFAULT);
    for (size_t i = 0; i < port_names.size(); i++) {
        auto* chip = lvgl_chip_create(chips_row);
        lv_label_set_text(lv_label_create(chip), port_names[i].c_str());
        lv_obj_set_user_data(chip, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(chip, onChipPressed, LV_EVENT_SHORT_CLICKED, ctx);
    }
    lvgl_grid_navigation_add(chips_row);
    ctx->chipsRow = chips_row;

    auto* scan_list = lvgl_card_create(main_wrapper);
    lv_obj_set_size(scan_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(scan_list, LV_LAYOUT_GRID);
    lvgl_grid_navigation_add(scan_list);
    lv_obj_set_hidden(scan_list, true);
    ctx->scanListWidget = scan_list;

    struct Device* dummy;
    const int32_t last_bus = getLastBusIndex();
    selectBus(ctx, getActivePortAtIndex(last_bus, &dummy) ? last_bus : 0);
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

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx;
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    stopScanningIfRunning(&ctx);
                    shouldClose = true;
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
    .id = "tactility.i2cscanner",
    .name = "I2C Scanner",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

uint32_t start() {
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_with_context(&context, &instanceId);
    return instanceId;
}

} // namespace
