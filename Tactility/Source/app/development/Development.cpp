#ifdef ESP_PLATFORM

#include <Tactility/Timer.h>
#include <Tactility/Tactility.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/Logger.h>
#include <Tactility/service/development/DevelopmentService.h>
#include <Tactility/service/development/DevelopmentSettings.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/service/wifi/Wifi.h>

#include <tactility/lvgl_symbols_shared.h>

#include <cstring>
#include <lvgl.h>

namespace tt::app::development {

static const auto LOGGER = Logger("Development");
extern const AppManifest manifest;

class DevelopmentApp final : public App {

    lv_obj_t* enableSwitch = nullptr;
    lv_obj_t* enableOnBootSwitch = nullptr;
    lv_obj_t* statusLabel = nullptr;
    std::shared_ptr<service::development::DevelopmentService> service;

    Timer timer = Timer(Timer::Type::Periodic, pdMS_TO_TICKS(1000), [this] {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        // TODO: There's a crash when this is called when the app is being destroyed
        if (lock.lock(lvgl::defaultLockTime) && lvgl::isStarted()) {
            updateViewState();
        }
    });

    static void onEnableSwitchChanged(lv_event_t* event) {
        lv_event_code_t code = lv_event_get_code(event);
        auto* widget = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (code == LV_EVENT_VALUE_CHANGED) {
            bool is_on = lv_obj_has_state(widget, LV_STATE_CHECKED);
            auto* app = static_cast<DevelopmentApp*>(lv_event_get_user_data(event));
            bool is_changed = is_on != app->service->isEnabled();
            if (is_changed) {
                app->service->setEnabled(is_on);
            }
        }
    }

    static void onEnableOnBootSwitchChanged(lv_event_t* event) {
        lv_event_code_t code = lv_event_get_code(event);
        auto* widget = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (code == LV_EVENT_VALUE_CHANGED) {
            bool is_on = lv_obj_has_state(widget, LV_STATE_CHECKED);
            bool is_changed = is_on != service::development::shouldEnableOnBoot();
            if (is_changed) {
                // Dispatch it, so file IO doesn't block the UI
                getMainDispatcher().dispatch([is_on] {
                    service::development::setEnableOnBoot(is_on);
                });
            }
        }
    }

    void updateViewState() {
        if (!service->isEnabled()) {
            lv_label_set_text(statusLabel, "Service disabled");
        } else if (service::wifi::getRadioState() != service::wifi::RadioState::ConnectionActive) {
            lv_label_set_text(statusLabel, "Waiting for connection...");
        } else { // enabled and connected to wifi
            auto ip = service::wifi::getIp();
            if (ip.empty()) {
                lv_label_set_text(statusLabel, "Waiting for IP...");
            } else {
                const std::string status = std::format("Available at {}", ip);
                lv_label_set_text(statusLabel, status.c_str());
            }
        }
    }

public:

    void onCreate(AppContext& appContext) override {
        service = service::development::findService();
        if (service == nullptr) {
            LOGGER.error("Service not found");
            stop(manifest.appId);
        }
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lv_obj_t* toolbar = lvgl::toolbar_create(parent, app);

        enableSwitch = lvgl::toolbar_add_switch_action(toolbar);
        lv_obj_add_event_cb(enableSwitch, onEnableSwitchChanged, LV_EVENT_VALUE_CHANGED, this);

        if (service->isEnabled()) {
            lv_obj_add_state(enableSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(enableSwitch, LV_STATE_CHECKED);
        }

        // Wrappers

        lv_obj_t* content_wrapper = lv_obj_create(parent);
        lv_obj_set_width(content_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(content_wrapper, 1);
        lv_obj_set_flex_flow(content_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(content_wrapper, 0, LV_STATE_DEFAULT);
        lvgl::obj_set_style_bg_invisible(content_wrapper);

        // Enable on boot

        lv_obj_t* enable_wrapper = lv_obj_create(content_wrapper);
        lv_obj_set_size(enable_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lvgl::obj_set_style_bg_invisible(enable_wrapper);
        lv_obj_set_style_border_width(enable_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(enable_wrapper, 0, LV_STATE_DEFAULT);

        lv_obj_t* enable_label = lv_label_create(enable_wrapper);
        lv_label_set_text(enable_label, "Enable on boot");
        lv_obj_align(enable_label, LV_ALIGN_LEFT_MID, 0, 0);

        enableOnBootSwitch = lv_switch_create(enable_wrapper);
        lv_obj_add_event_cb(enableOnBootSwitch, onEnableOnBootSwitchChanged, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_align(enableOnBootSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        if (service::development::shouldEnableOnBoot()) {
            lv_obj_add_state(enableOnBootSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(enableOnBootSwitch, LV_STATE_CHECKED);
        }

        // Status

        statusLabel = lv_label_create(content_wrapper);

        // Warning

        auto warning_label = lv_label_create(content_wrapper);
        lv_label_set_text(warning_label, "This feature is experimental and uses an unsecured http connection.");
        lv_obj_set_width(warning_label, LV_PCT(100));
        lv_label_set_long_mode(warning_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(warning_label, lv_color_make(0xff, 0xff, 00), LV_STATE_DEFAULT);

        updateViewState();

        timer.start();
    }

    void onHide(AppContext& appContext) override {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        // Ensure that the update isn't already happening
        lock.lock();
        timer.stop();
    }
};

extern const AppManifest manifest = {
    .appId = "Development",
    .appName = "Development",
    .appIcon = LVGL_SYMBOL_DEVICES,
    .appCategory = Category::Settings,
    .createApp = create<DevelopmentApp>
};

void start() {
    app::start(manifest.appId);
}

} // namespace

#endif // ESP_PLATFORM