#include <Tactility/Tactility.h>
#include <Tactility/TactilityConfig.h>


#if TT_FEATURE_SCREENSHOT_ENABLED

#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <Tactility/kernel/Platform.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/screenshot/Screenshot.h>
#include <Tactility/Timer.h>

#include <tactility/lvgl_icon_shared.h>

namespace tt::app::screenshot {

static const auto LOGGER = Logger("Screenshot");

extern const AppManifest manifest;

class ScreenshotApp final : public App {

    lv_obj_t* modeDropdown = nullptr;
    lv_obj_t* pathTextArea = nullptr;
    lv_obj_t* startStopButtonLabel = nullptr;
    lv_obj_t* timerWrapper = nullptr;
    lv_obj_t* delayTextArea = nullptr;
    std::unique_ptr<Timer> updateTimer;

    void createTimerSettingsWidgets(lv_obj_t* parent);
    void createModeSettingWidgets(lv_obj_t* parent);
    void createFilePathWidgets(lv_obj_t* parent);

    void updateScreenshotMode();

public:

    ScreenshotApp();
    ~ScreenshotApp() override;

    void onShow(AppContext& app, lv_obj_t* parent) override;
    void onStartPressed();
    void onModeSet();
    void onTimerTick();
};


/** Returns the app data if the app is active. Note that this could clash if the same app is started twice and a background thread is slow. */
std::shared_ptr<ScreenshotApp> optApp() {
    auto appContext = getCurrentAppContext();
    if (appContext != nullptr && appContext->getManifest().appId == manifest.appId) {
        return std::static_pointer_cast<ScreenshotApp>(appContext->getApp());
    } else {
        return nullptr;
    }
}

static void onStartPressedCallback(lv_event_t* event) {
    auto app = optApp();
    if (app != nullptr) {
        app->onStartPressed();
    }
}

static void onModeSetCallback(lv_event_t* event) {
    auto app = optApp();
    if (app != nullptr) {
        app->onModeSet();
    }
}

ScreenshotApp::ScreenshotApp() {
    updateTimer = std::make_unique<Timer>(Timer::Type::Periodic, 500 / portTICK_PERIOD_MS, [this] {
        onTimerTick();
    });
}

ScreenshotApp::~ScreenshotApp() {
    if (updateTimer->isRunning()) {
        updateTimer->stop();
    }
}

void ScreenshotApp::onTimerTick() {
    auto lock = lvgl::getSyncLock()->asScopedLock();
    if (lock.lock(lvgl::defaultLockTime)) {
        updateScreenshotMode();
    }
}

void ScreenshotApp::onModeSet() {
    updateScreenshotMode();
}

void ScreenshotApp::onStartPressed() {
    auto service = service::screenshot::optScreenshotService();
    if (service == nullptr) {
        LOGGER.error("Service not found/running");
        return;
    }

    if (service->isTaskStarted()) {
        LOGGER.info("Stop screenshot");
        service->stop();
    } else {
        uint32_t selected = lv_dropdown_get_selected(modeDropdown);
        const char* path = lv_textarea_get_text(pathTextArea);
        if (selected == 0) {
            LOGGER.info("Start timed screenshots");
            const char* delay_text = lv_textarea_get_text(delayTextArea);
            int delay = atoi(delay_text);
            if (delay > 0) {
                service->startTimed(path, delay, 1);
            } else {
                LOGGER.warn("Ignored screenshot start because delay was 0");
            }
        } else {
            LOGGER.info("Start app screenshots");
            service->startApps(path);
        }
    }

    updateScreenshotMode();
}

void ScreenshotApp::updateScreenshotMode() {
    auto service = service::screenshot::optScreenshotService();
    if (service == nullptr) {
        LOGGER.error("Service not found/running");
        return;
    }

    lv_obj_t* label = startStopButtonLabel;
    if (service->isTaskStarted()) {
        lv_label_set_text(label, "Stop");
    } else {
        lv_label_set_text(label, "Start");
    }

    uint32_t selected = lv_dropdown_get_selected(modeDropdown);
    if (selected == 0) { // Timer
        lv_obj_remove_flag(timerWrapper, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(timerWrapper, LV_OBJ_FLAG_HIDDEN);
    }
}


void ScreenshotApp::createModeSettingWidgets(lv_obj_t* parent) {
    auto service = service::screenshot::optScreenshotService();
    if (service == nullptr) {
        LOGGER.error("Service not found/running");
        return;
    }

    auto* mode_wrapper = lv_obj_create(parent);
    lv_obj_set_size(mode_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(mode_wrapper, 0, 0);
    lv_obj_set_style_border_width(mode_wrapper, 0, 0);

    auto* mode_label = lv_label_create(mode_wrapper);
    lv_label_set_text(mode_label, "Mode:");
    lv_obj_align(mode_label, LV_ALIGN_LEFT_MID, 0, 0);

    modeDropdown = lv_dropdown_create(mode_wrapper);
    lv_dropdown_set_options(modeDropdown, "Timer\nApp start");
    lv_obj_align_to(modeDropdown, mode_label, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_set_style_border_color(modeDropdown, lv_color_hex(0xFAFAFA), LV_PART_MAIN);
    lv_obj_set_style_border_width(modeDropdown, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(modeDropdown, onModeSetCallback, LV_EVENT_VALUE_CHANGED, nullptr);
    service::screenshot::Mode mode = service->getMode();
    if (mode == service::screenshot::Mode::Apps) {
        lv_dropdown_set_selected(modeDropdown, 1);
    }

    auto* button = lv_button_create(mode_wrapper);
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(button, &onStartPressedCallback, LV_EVENT_SHORT_CLICKED, nullptr);
    startStopButtonLabel = lv_label_create(button);
    lv_obj_align(startStopButtonLabel, LV_ALIGN_CENTER, 0, 0);
}

void ScreenshotApp::createFilePathWidgets(lv_obj_t* parent) {
    auto* path_wrapper = lv_obj_create(parent);
    lv_obj_set_size(path_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(path_wrapper, 0, 0);
    lv_obj_set_style_border_width(path_wrapper, 0, 0);
    lv_obj_set_flex_flow(path_wrapper, LV_FLEX_FLOW_ROW);

    auto* label_wrapper = lv_obj_create(path_wrapper);
    lv_obj_set_style_border_width(label_wrapper, 0, 0);
    lv_obj_set_style_pad_all(label_wrapper, 0, 0);
    lv_obj_set_size(label_wrapper, 44, 36);
    auto* path_label = lv_label_create(label_wrapper);
    lv_label_set_text(path_label, "Path:");
    lv_obj_align(path_label, LV_ALIGN_LEFT_MID, 0, 0);

    pathTextArea = lv_textarea_create(path_wrapper);
    lv_textarea_set_one_line(pathTextArea, true);
    lv_obj_set_flex_grow(pathTextArea, 1);
    if (kernel::getPlatform() == kernel::PlatformEsp) {
        auto sdcard_devices = tt::hal::findDevices<tt::hal::sdcard::SdCardDevice>(tt::hal::Device::Type::SdCard);
        if (sdcard_devices.size() > 1) {
            LOGGER.warn("Found multiple SD card devices - picking first");
        }
        if (!sdcard_devices.empty() && sdcard_devices.front()->isMounted()) {
            std::string lvgl_mount_path = lvgl::PATH_PREFIX + sdcard_devices.front()->getMountPath();
            lv_textarea_set_text(pathTextArea, lvgl_mount_path.c_str());
        } else {
            lv_textarea_set_text(pathTextArea, "Error: no SD card");
        }
    } else { // PC
        lv_textarea_set_text(pathTextArea, lvgl::PATH_PREFIX);
    }
}

void ScreenshotApp::createTimerSettingsWidgets(lv_obj_t* parent) {
    timerWrapper = lv_obj_create(parent);
    lv_obj_set_size(timerWrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(timerWrapper, 0, 0);
    lv_obj_set_style_border_width(timerWrapper, 0, 0);

    auto* delay_wrapper = lv_obj_create(timerWrapper);
    lv_obj_set_size(delay_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(delay_wrapper, 0, 0);
    lv_obj_set_style_border_width(delay_wrapper, 0, 0);
    lv_obj_set_flex_flow(delay_wrapper, LV_FLEX_FLOW_ROW);

    auto* delay_label_wrapper = lv_obj_create(delay_wrapper);
    lv_obj_set_style_border_width(delay_label_wrapper, 0, 0);
    lv_obj_set_style_pad_all(delay_label_wrapper, 0, 0);
    lv_obj_set_size(delay_label_wrapper, 44, 36);
    auto* delay_label = lv_label_create(delay_label_wrapper);
    lv_label_set_text(delay_label, "Delay:");
    lv_obj_align(delay_label, LV_ALIGN_LEFT_MID, 0, 0);

    delayTextArea = lv_textarea_create(delay_wrapper);
    lv_textarea_set_one_line(delayTextArea, true);
    lv_textarea_set_accepted_chars(delayTextArea, "0123456789");
    lv_textarea_set_text(delayTextArea, "10");
    lv_obj_set_flex_grow(delayTextArea, 1);

    auto* delay_unit_label_wrapper = lv_obj_create(delay_wrapper);
    lv_obj_set_style_border_width(delay_unit_label_wrapper, 0, 0);
    lv_obj_set_style_pad_all(delay_unit_label_wrapper, 0, 0);
    lv_obj_set_size(delay_unit_label_wrapper, LV_SIZE_CONTENT, 36);
    auto* delay_unit_label = lv_label_create(delay_unit_label_wrapper);
    lv_obj_align(delay_unit_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(delay_unit_label, "seconds");
}

void ScreenshotApp::onShow(AppContext& appContext, lv_obj_t* parent) {
    if (updateTimer->isRunning()) {
        updateTimer->stop();
    }

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl::toolbar_create(parent, appContext);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

    createModeSettingWidgets(wrapper);
    createFilePathWidgets(wrapper);
    createTimerSettingsWidgets(wrapper);

    updateScreenshotMode();

    if (!updateTimer->isRunning()) {
        updateTimer->start();
    }
}

extern const AppManifest manifest = {
    .appId = "Screenshot",
    .appName = "Screenshot",
    .appIcon = LVGL_ICON_SHARED_IMAGE,
    .appCategory = Category::System,
    .createApp = create<ScreenshotApp>
};

} // namespace

#endif
