#include <Tactility/app/i2cscanner/I2cScannerPrivate.h>
#include <Tactility/app/i2cscanner/I2cHelpers.h>

#include <Tactility/Assets.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/hal/i2c/I2cDevice.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/Preferences.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>

#include <format>

namespace tt::app::i2cscanner {

extern const AppManifest manifest;

class I2cScannerApp final : public App {

    const Logger logger = Logger("I2cScanner");

    static constexpr auto* START_SCAN_TEXT = "Scan";
    static constexpr auto* STOP_SCAN_TEXT = "Stop scan";

    // Core
    RecursiveMutex mutex;
    std::unique_ptr<Timer> scanTimer = nullptr;
    // State
    ScanState scanState = ScanStateInitial;
    i2c_port_t port = I2C_NUM_0;
    std::vector<uint8_t> scannedAddresses;
    // Widgets
    lv_obj_t* scanButtonLabelWidget = nullptr;
    lv_obj_t* portDropdownWidget = nullptr;
    lv_obj_t* scanListWidget = nullptr;

    static void setLastBusIndex(int32_t index);
    static int32_t getLastBusIndex();

    void selectBus(int32_t selected);

    static void onSelectBusCallback(lv_event_t* event);
    static void onPressScanCallback(lv_event_t* event);
    static void onScanTimerCallback();

    void onSelectBus(lv_event_t* event);
    void onPressScan(lv_event_t* event);
    void onScanTimer();

    bool shouldStopScanTimer();
    bool getPort(i2c_port_t* outPort);
    bool addAddressToList(uint8_t address);
    bool hasScanThread();
    void startScanning();
    void stopScanning();

    void updateViews();
    void updateViewsSafely();

    void onScanTimerFinished();

public:

    void onShow(AppContext& app, lv_obj_t* parent) override;
    void onHide(AppContext& app) override;
};

/** Returns the app data if the app is active. Note that this could clash if the same app is started twice and a background thread is slow. */
std::shared_ptr<I2cScannerApp> optApp() {
    auto appContext = getCurrentAppContext();
    if (appContext != nullptr && appContext->getManifest().appId == manifest.appId) {
        return std::static_pointer_cast<I2cScannerApp>(appContext->getApp());
    } else {
        return nullptr;
    }
}

#define PREFERENCES_BUS_INDEX_KEY "bus"

void I2cScannerApp::setLastBusIndex(int32_t index) {
    auto prefs = Preferences("i2c_scanner");
    prefs.putInt32(PREFERENCES_BUS_INDEX_KEY, index);
}

int32_t I2cScannerApp::getLastBusIndex() {
    auto prefs = Preferences("i2c_scanner");
    int32_t index = 0;
    prefs.optInt32(PREFERENCES_BUS_INDEX_KEY, index);
    return index;
}

// region Lifecycle

void I2cScannerApp::onShow(AppContext& app, lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lvgl::toolbar_create(parent, app);

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
    lv_obj_add_event_cb(scan_button, onPressScanCallback, LV_EVENT_SHORT_CLICKED, this);
    auto* scan_button_label = lv_label_create(scan_button);
    lv_obj_align(scan_button_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(scan_button_label, START_SCAN_TEXT);
    scanButtonLabelWidget = scan_button_label;

    auto* port_dropdown = lv_dropdown_create(wrapper);
    std::string dropdown_items = getPortNamesForDropdown();
    lv_dropdown_set_options(port_dropdown, dropdown_items.c_str());
    lv_obj_set_width(port_dropdown, LV_PCT(48));
    lv_obj_align(port_dropdown, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_border_color(port_dropdown, lv_color_hex(0xFAFAFA), LV_PART_MAIN);
    lv_obj_set_style_border_width(port_dropdown, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(port_dropdown, onSelectBusCallback, LV_EVENT_VALUE_CHANGED, this);
    auto selected_bus = getLastBusIndex();
    lv_dropdown_set_selected(port_dropdown, selected_bus);
    portDropdownWidget = port_dropdown;

    auto* scan_list = lv_list_create(main_wrapper);
    lv_obj_set_style_margin_top(scan_list, 8, 0);
    lv_obj_set_width(scan_list, LV_PCT(100));
    lv_obj_set_height(scan_list, LV_SIZE_CONTENT);
    lv_obj_add_flag(scan_list, LV_OBJ_FLAG_HIDDEN);
    scanListWidget = scan_list;

    int32_t first_port;
    if (getActivePortAtIndex(0, first_port)) {
        lv_dropdown_set_selected(port_dropdown, 0);
        selectBus(0);
    }
}

void I2cScannerApp::onHide(AppContext& app) {
    bool isRunning = false;
    if (mutex.lock(250 / portTICK_PERIOD_MS)) {
        auto* timer = scanTimer.get();
        if (timer != nullptr) {
            isRunning = timer->isRunning();
        }
        mutex.unlock();
    } else {
        return;
    }

    if (isRunning) {
        stopScanning();
    }
}

// endregion Lifecycle

// region Callbacks

void I2cScannerApp::onSelectBusCallback(lv_event_t* event) {
    auto* app = (I2cScannerApp*)lv_event_get_user_data(event);
    if (app != nullptr) {
        app->onSelectBus(event);
    }
}

void I2cScannerApp::onPressScanCallback(lv_event_t* event) {
    auto* app = (I2cScannerApp*)lv_event_get_user_data(event);
    if (app != nullptr) {
        app->onPressScan(event);
    }
}

// endregion Callbacks

bool I2cScannerApp::getPort(i2c_port_t* outPort) {
    if (mutex.lock(100 / portTICK_PERIOD_MS)) {
        *outPort = this->port;
        mutex.unlock();
        return true;
    } else {
        logger.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "getPort");
        return false;
    }
}

bool I2cScannerApp::addAddressToList(uint8_t address) {
    if (mutex.lock(100 / portTICK_PERIOD_MS)) {
        scannedAddresses.push_back(address);
        mutex.unlock();
        return true;
    } else {
        logger.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "addAddressToList");
        return false;
    }
}

bool I2cScannerApp::shouldStopScanTimer() {
    if (mutex.lock(100 / portTICK_PERIOD_MS)) {
        bool is_scanning = scanState == ScanStateScanning;
        mutex.unlock();
        return !is_scanning;
    } else {
        return true;
    }
}

void I2cScannerApp::onScanTimer() {
    logger.info("Scan thread started");

    i2c_port_t safe_port;
    if (!getPort(&safe_port)) {
        logger.error("Failed to get I2C port");
        onScanTimerFinished();
        return;
    }

    if (!hal::i2c::isStarted(safe_port)) {
        logger.error("I2C port not started");
        onScanTimerFinished();
        return;
    }

    for (uint8_t address = 0; address < 128; ++address) {
        if (hal::i2c::masterHasDeviceAtAddress(safe_port, address, 10 / portTICK_PERIOD_MS)) {
            logger.info("Found device at address 0x{:02X}", address);
            if (!shouldStopScanTimer()) {
                addAddressToList(address);
            } else {
                break;
            }
        }

        if (shouldStopScanTimer()) {
            break;
        }
    }

    logger.info("Scan thread finalizing");

    onScanTimerFinished();

    logger.info("Scan timer done");
}

bool I2cScannerApp::hasScanThread() {
    bool has_thread;
    if (mutex.lock(100 / portTICK_PERIOD_MS)) {
        has_thread = scanTimer != nullptr;
        mutex.unlock();
        return has_thread;
    } else {
        // Unsafe way
        logger.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "hasScanTimer");
        return scanTimer != nullptr;
    }
}

void I2cScannerApp::startScanning() {
    if (hasScanThread()) {
        stopScanning();
    }

    if (mutex.lock(100 / portTICK_PERIOD_MS)) {
        scannedAddresses.clear();

        lv_obj_add_flag(scanListWidget, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clean(scanListWidget);

        scanState = ScanStateScanning;
        scanTimer = std::make_unique<Timer>(Timer::Type::Once, 10, [this]{
            onScanTimer();
        });
        scanTimer->start();
        mutex.unlock();
    } else {
        logger.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "startScanning");
    }
}
void I2cScannerApp::stopScanning() {
    if (mutex.lock(250 / portTICK_PERIOD_MS)) {
        assert(scanTimer != nullptr);
        scanState = ScanStateStopped;
        mutex.unlock();
    } else {
        logger.error(LOG_MESSAGE_MUTEX_LOCK_FAILED);
    }
}

void I2cScannerApp::onSelectBus(lv_event_t* event) {
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
    uint32_t selected = lv_dropdown_get_selected(dropdown);
    selectBus(selected);
}

void I2cScannerApp::selectBus(int32_t selected) {
    int32_t found_port;
    if (!getActivePortAtIndex(selected, found_port)) {
        return;
    }

    if (mutex.lock(100 / portTICK_PERIOD_MS)) {
        scannedAddresses.clear();
        port = static_cast<i2c_port_t>(found_port);
        scanState = ScanStateInitial;
        mutex.unlock();
    }

    logger.info("Selected {}", selected);
    setLastBusIndex(selected);

    startScanning();

    updateViews();
}

void I2cScannerApp::onPressScan(lv_event_t* event) {
    if (scanState == ScanStateScanning) {
        stopScanning();
    } else {
        startScanning();
    }
    updateViews();
}

static bool findDeviceName(const std::vector<std::shared_ptr<hal::i2c::I2cDevice>>& devices, i2c_port_t port, uint8_t address, std::string& outName) {
    for (auto& device : devices) {
        if (device->getPort() == port && device->getAddress() == address) {
            outName = device->getName();
            return true;
        }
    }
    return false;
}

void I2cScannerApp::updateViews() {
    if (mutex.lock(100 / portTICK_PERIOD_MS)) {
        if (scanState == ScanStateScanning) {
            lv_label_set_text(scanButtonLabelWidget, STOP_SCAN_TEXT);
            lv_obj_remove_flag(portDropdownWidget, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_label_set_text(scanButtonLabelWidget, START_SCAN_TEXT);
            lv_obj_add_flag(portDropdownWidget, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_clean(scanListWidget);
        if (scanState == ScanStateStopped) {
            lv_obj_remove_flag(scanListWidget, LV_OBJ_FLAG_HIDDEN);

            auto devices = hal::findDevices<hal::i2c::I2cDevice>(hal::Device::Type::I2c);

            if (!scannedAddresses.empty()) {
                for (auto address: scannedAddresses) {
                    std::string address_text = getAddressText(address);
                    std::string device_name;
                    if (findDeviceName(devices, port, address, device_name)) {
                        auto text = std::format("{} - {}", address_text, device_name);
                        lv_list_add_text(scanListWidget, text.c_str());
                    } else {
                        lv_list_add_text(scanListWidget, address_text.c_str());
                    }
                }
            } else {
                lv_list_add_text(scanListWidget, "No devices found");
            }
        } else {
            lv_obj_add_flag(scanListWidget, LV_OBJ_FLAG_HIDDEN);
        }

        mutex.unlock();
    } else {
        logger.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "updateViews");
    }
}

void I2cScannerApp::updateViewsSafely() {
    if (lvgl::lock(200 / portTICK_PERIOD_MS)) {
        updateViews();
        lvgl::unlock();
    } else {
        logger.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "updateViewsSafely");
    }
}

void I2cScannerApp::onScanTimerFinished() {
    if (mutex.lock(100 / portTICK_PERIOD_MS)) {
        if (scanState == ScanStateScanning) {
            scanState = ScanStateStopped;
        }
        mutex.unlock();

        updateViewsSafely();
    } else {
        logger.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "onScanTimerFinished");
    }
}

extern const AppManifest manifest = {
    .appId = "I2cScanner",
    .appName = "I2C Scanner",
    .appIcon = TT_ASSETS_APP_ICON_I2C_SETTINGS,
    .appCategory = Category::System,
    .createApp = create<I2cScannerApp>
};

LaunchId start() {
    return app::start(manifest.appId);
}

} // namespace
