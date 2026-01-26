#include <Tactility/Tactility.h>

#include <Tactility/Timer.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/Logger.h>
#include <Tactility/service/gps/GpsService.h>
#include <Tactility/service/gps/GpsState.h>
#include <Tactility/service/loader/Loader.h>

#include <cstring>
#include <format>
#include <lvgl.h>

namespace tt::app::addgps {
extern AppManifest manifest;
}

namespace tt::app::gpssettings {

extern const AppManifest manifest;

class GpsSettingsApp final : public App {

    const Logger logger = Logger("GpsSettings");

    std::unique_ptr<Timer> timer;
    std::shared_ptr<GpsSettingsApp*> appReference = std::make_shared<GpsSettingsApp*>(this);
    lv_obj_t* statusWrapper = nullptr;
    lv_obj_t* statusLabelWidget = nullptr;
    lv_obj_t* switchWidget = nullptr;
    lv_obj_t* spinnerWidget = nullptr;
    lv_obj_t* infoContainerWidget = nullptr;
    lv_obj_t* gpsConfigWrapper = nullptr;
    lv_obj_t* addGpsWrapper = nullptr;
    bool hasSetInfo = false;
    PubSub<service::gps::State>::SubscriptionHandle serviceStateSubscription = nullptr;
    std::shared_ptr<service::gps::GpsService> service;

    void onServiceStateChanged() {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        if (lock.lock(100 / portTICK_PERIOD_MS)) {
            if (!updateTimerState()) {
                updateViews();
            }
        }
    }

    static void onGpsToggledCallback(lv_event_t* event) {
        auto* app = (GpsSettingsApp*)lv_event_get_user_data(event);
        app->onGpsToggled(event);
    }

    static void onAddGpsCallback(lv_event_t* event) {
        auto* app = (GpsSettingsApp*)lv_event_get_user_data(event);
        app->onAddGps();
    }

    void onAddGps() {
        app::start(addgps::manifest.appId);
    }

    void startReceivingUpdates() {
        timer->start();
        updateViews();
    }

    void stopReceivingUpdates() {
        timer->stop();
        updateViews();
    }

    void createInfoView(hal::gps::GpsModel model) {
        auto* label = lv_label_create(infoContainerWidget);
        if (model == hal::gps::GpsModel::Unknown) {
            lv_label_set_text(label, "Model: auto-detect");
        } else {
            lv_label_set_text_fmt(label, "Model: %s", toString(model));
        }
    }

    static void onDeleteConfiguration(lv_event_t* event) {
        auto* app = (GpsSettingsApp*)lv_event_get_user_data(event);

        auto* button = lv_event_get_target_obj(event);
        auto index_as_voidptr = lv_obj_get_user_data(button); // config index
        int index;
        // TODO: Find a better way to cast void* to int, or find a different way to pass the index
        memcpy(&index, &index_as_voidptr, sizeof(int));

        std::vector<tt::hal::gps::GpsConfiguration> configurations;
        auto gps_service = service::gps::findGpsService();
        if (gps_service && gps_service->getGpsConfigurations(configurations)) {
            Logger("GpsSettings").info("Found service and configs {} {}", index, configurations.size());
            if (index < configurations.size()) {
                if (gps_service->removeGpsConfiguration(configurations[index])) {
                    app->updateViews();
                } else {
                    alertdialog::start("Error", "Failed to remove configuration");
                }
            }
        }
    }

    void createGpsView(const hal::gps::GpsConfiguration& configuration, int index) {
        auto* wrapper = lv_obj_create(gpsConfigWrapper);
        lv_obj_set_size(wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_margin_hor(wrapper, 0, 0);
        lv_obj_set_style_margin_bottom(wrapper, 8, 0);

        // Left wrapper

        auto* left_wrapper = lv_obj_create(wrapper);
        lv_obj_set_style_border_width(left_wrapper, 0, 0);
        lv_obj_set_style_pad_all(left_wrapper, 0, 0);
        lv_obj_set_size(left_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(left_wrapper, 1);
        lv_obj_set_flex_flow(left_wrapper, LV_FLEX_FLOW_COLUMN);

        auto* uart_label = lv_label_create(left_wrapper);
        lv_label_set_text_fmt(uart_label, "UART: %s", configuration.uartName);

        auto* baud_label = lv_label_create(left_wrapper);
        lv_label_set_text_fmt(baud_label, "Baud: %lu", configuration.baudRate);

        auto* model_label = lv_label_create(left_wrapper);
        if (configuration.model == hal::gps::GpsModel::Unknown) {
            lv_label_set_text(model_label, "Model: auto-detect");
        } else {
            lv_label_set_text_fmt(model_label, "Model: %s", toString(configuration.model));
        }

        // Right wrapper
        auto* right_wrapper = lv_obj_create(wrapper);
        lv_obj_set_style_border_width(right_wrapper, 0, 0);
        lv_obj_set_style_pad_all(right_wrapper, 0, 0);
        lv_obj_set_size(right_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(right_wrapper, LV_FLEX_FLOW_COLUMN);

        auto* delete_button = lv_button_create(right_wrapper);
        lv_obj_add_event_cb(delete_button, onDeleteConfiguration, LV_EVENT_SHORT_CLICKED, this);
        lv_obj_set_user_data(delete_button, reinterpret_cast<void*>(index));
        auto* delete_label = lv_label_create(delete_button);
        lv_label_set_text_fmt(delete_label, LV_SYMBOL_TRASH);
    }

    void updateViews() {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        if (lock.lock(100 / portTICK_PERIOD_MS)) {
            auto state = service->getState();

            // Update toolbar
            switch (state) {
                case service::gps::State::OnPending:
                    logger.debug("OnPending");
                    lv_obj_remove_flag(spinnerWidget, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_state(switchWidget, LV_STATE_CHECKED);
                    lv_obj_add_state(switchWidget, LV_STATE_DISABLED);
                    lv_obj_remove_flag(statusWrapper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(gpsConfigWrapper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(addGpsWrapper, LV_OBJ_FLAG_HIDDEN);
                    break;
                case service::gps::State::On:
                    logger.debug("On");
                    lv_obj_add_flag(spinnerWidget, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_state(switchWidget, LV_STATE_CHECKED);
                    lv_obj_remove_state(switchWidget, LV_STATE_DISABLED);
                    lv_obj_remove_flag(statusWrapper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(gpsConfigWrapper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(addGpsWrapper, LV_OBJ_FLAG_HIDDEN);
                    break;
                case service::gps::State::OffPending:
                    logger.debug("OffPending");
                    lv_obj_remove_flag(spinnerWidget, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_state(switchWidget, LV_STATE_CHECKED);
                    lv_obj_add_state(switchWidget, LV_STATE_DISABLED);
                    lv_obj_add_flag(statusWrapper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(gpsConfigWrapper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(addGpsWrapper, LV_OBJ_FLAG_HIDDEN);
                    break;
                case service::gps::State::Off:
                    logger.debug("Off");
                    lv_obj_add_flag(spinnerWidget, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_state(switchWidget, LV_STATE_CHECKED);
                    lv_obj_remove_state(switchWidget, LV_STATE_DISABLED);
                    lv_obj_add_flag(statusWrapper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(gpsConfigWrapper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(addGpsWrapper, LV_OBJ_FLAG_HIDDEN);
                    break;
            }

            // Update status label and device info
            if (state == service::gps::State::On) {
                if (!hasSetInfo) {
                    auto devices = hal::findDevices<hal::gps::GpsDevice>(hal::Device::Type::Gps);
                    for (auto& device : devices) {
                        createInfoView(device->getModel());
                        hasSetInfo = true;
                    }
                }

                minmea_sentence_rmc rmc;
                if (service->getCoordinates(rmc)) {
                    minmea_float latitude = { rmc.latitude.value, rmc.latitude.scale };
                    minmea_float longitude = { rmc.longitude.value, rmc.longitude.scale };
                    auto label_text = std::format("LAT {}\nLON {}", minmea_tocoord(&latitude), minmea_tocoord(&longitude));
                    lv_label_set_text(statusLabelWidget, label_text.c_str());
                } else {
                    lv_label_set_text(statusLabelWidget, "Acquiring lock...");
                }
                lv_obj_remove_flag(statusLabelWidget, LV_OBJ_FLAG_HIDDEN);
            } else {
                if (hasSetInfo) {
                    lv_obj_clean(infoContainerWidget);
                    hasSetInfo = false;
                }

                lv_obj_add_flag(statusLabelWidget, LV_OBJ_FLAG_HIDDEN);
            }

            lv_obj_clean(gpsConfigWrapper);
            std::vector<tt::hal::gps::GpsConfiguration> configurations;
            auto gps_service = tt::service::gps::findGpsService();
            if (gps_service && gps_service->getGpsConfigurations(configurations)) {
                int index = 0;
                for (auto& configuration : configurations) {
                    createGpsView(configuration, index++);
                }
            }
        }
    }

    /** @return true if the views were updated */
    bool updateTimerState() {
        bool is_on = service->getState() == service::gps::State::On;
        if (is_on && !timer->isRunning()) {
            startReceivingUpdates();
            return true;
        } else if (!is_on && timer->isRunning()) {
            stopReceivingUpdates();
            return true;
        } else {
            return false;
        }
    }

    void onGpsToggled(lv_event_t* event) {
        bool wants_on = lv_obj_has_state(switchWidget, LV_STATE_CHECKED);
        auto state = service->getState();
        bool is_on = (state == service::gps::State::On) || (state == service::gps::State::OnPending);

        if (wants_on != is_on) {
            // start/stop are potentially blocking calls, so we use a dispatcher to not block the UI
            if (wants_on) {
                getMainDispatcher().dispatch([this] {
                    service->startReceiving();
                });
            } else {
                getMainDispatcher().dispatch([this] {
                    service->stopReceiving();
                });
            }
        }
    }

public:

    GpsSettingsApp() {
        timer = std::make_unique<Timer>(Timer::Type::Periodic, kernel::secondsToTicks(1), [this] {
            updateViews();
        });
        service = service::gps::findGpsService();
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        auto* toolbar = lvgl::toolbar_create(parent, app);

        spinnerWidget = lvgl::toolbar_add_spinner_action(toolbar);
        lv_obj_add_flag(spinnerWidget, LV_OBJ_FLAG_HIDDEN);

        switchWidget = lvgl::toolbar_add_switch_action(toolbar);
        lv_obj_add_event_cb(switchWidget, onGpsToggledCallback, LV_EVENT_VALUE_CHANGED, this);

        auto* main_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(main_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(main_wrapper, 1);
        lv_obj_set_style_border_width(main_wrapper, 0, 0);
        lv_obj_set_style_pad_all(main_wrapper, 0, 0);

        statusWrapper = lv_obj_create(main_wrapper);
        lv_obj_set_width(statusWrapper, LV_PCT(100));
        lv_obj_set_height(statusWrapper, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(statusWrapper, 0, 0);
        lv_obj_set_style_border_width(statusWrapper, 0, 0);

        statusLabelWidget = lv_label_create(statusWrapper);
        lv_obj_align(statusLabelWidget, LV_ALIGN_TOP_LEFT, 0, 0);

        infoContainerWidget = lv_obj_create(statusWrapper);
        lv_obj_align_to(infoContainerWidget, statusLabelWidget, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
        lv_obj_set_size(infoContainerWidget, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(infoContainerWidget, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(infoContainerWidget, 0, 0);
        lv_obj_set_style_pad_all(infoContainerWidget, 0, 0);
        hasSetInfo = false;

        serviceStateSubscription = service->getStatePubsub()->subscribe([this](auto) {
            onServiceStateChanged();
        });

        gpsConfigWrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(gpsConfigWrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(gpsConfigWrapper, 0, 0);
        lv_obj_set_style_margin_all(gpsConfigWrapper, 0, 0);
        lv_obj_set_style_pad_bottom(gpsConfigWrapper, 0, 0);

        addGpsWrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(addGpsWrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(addGpsWrapper, 0, 0);
        lv_obj_set_style_pad_all(addGpsWrapper, 0, 0);
        lv_obj_set_style_margin_top(addGpsWrapper, 0, 0);
        lv_obj_set_style_margin_bottom(addGpsWrapper, 8, 0);

        auto* add_gps_button = lv_button_create(addGpsWrapper);
        auto* add_gps_label = lv_label_create(add_gps_button);
        lv_label_set_text(add_gps_label, "Add GPS");
        lv_obj_add_event_cb(add_gps_button, onAddGpsCallback, LV_EVENT_SHORT_CLICKED, this);
        lv_obj_align(add_gps_button, LV_ALIGN_TOP_MID, 0, 0);

        updateTimerState();
        updateViews();
    }

    void onHide(AppContext& app) override {
        service->getStatePubsub()->unsubscribe(serviceStateSubscription);
        serviceStateSubscription = nullptr;
    }
};

extern const AppManifest manifest = {
    .appId = "GpsSettings",
    .appName = "GPS",
    .appIcon = LV_SYMBOL_GPS,
    .appCategory = Category::Settings,
    .createApp = create<GpsSettingsApp>
};

void start() {
    app::start(manifest.appId);
}

} // namespace
