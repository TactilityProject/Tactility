#include <Tactility/lvgl/Statusbar.h>

#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>
#include <Tactility/Timer.h>
#include <tactility/check.h>
#include <Tactility/hal/power/PowerDevice.h>
#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServicePaths.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/gps/GpsService.h>
#include <Tactility/service/wifi/Wifi.h>

#include <tactility/lvgl_symbols_statusbar.h>

namespace tt::service::statusbar {

static const auto LOGGER = Logger("StatusbarService");

// GPS
extern const ServiceManifest manifest;

const char* getWifiStatusIconForRssi(int rssi) {
    if (rssi >= -60) {
        return LVGL_SYMBOL_SIGNAL_WIFI_4_BAR;
    } else if (rssi >= -70) {
        return LVGL_SYMBOL_NETWORK_WIFI_3_BAR;
    } else if (rssi >= -80) {
        return LVGL_SYMBOL_NETWORK_WIFI_2_BAR;
    } else if (rssi >= -90) {
        return LVGL_SYMBOL_NETWORK_WIFI_1_BAR;
    } else {
        return LVGL_SYMBOL_SIGNAL_WIFI_BAD;
    }
}

static const char* getWifiStatusIcon(wifi::RadioState state) {
    int rssi;
    switch (state) {
        using enum wifi::RadioState;
        case On:
        case OnPending:
        case ConnectionPending:
            return LVGL_SYMBOL_SIGNAL_WIFI_0_BAR;
        case OffPending:
        case Off:
            return LVGL_SYMBOL_SIGNAL_WIFI_OFF;
        case ConnectionActive:
            rssi = wifi::getRssi();
            return getWifiStatusIconForRssi(rssi);
        default:
            check(false, "not implemented");
    }
}

static const char* getSdCardStatusIcon(hal::sdcard::SdCardDevice::State state) {
    switch (state) {
        using enum hal::sdcard::SdCardDevice::State;
        case Mounted:
            return LVGL_SYMBOL_SD_CARD;
        case Error:
        case Unmounted:
        case Timeout:
            return LVGL_SYMBOL_SD_CARD_ALERT;
        default:
            check(false, "Unhandled SdCard state");
    }
}

static const char* getPowerStatusIcon() {
    // TODO: Support multiple power devices?
    std::shared_ptr<hal::power::PowerDevice> power;
    hal::findDevices<hal::power::PowerDevice>(hal::Device::Type::Power, [&power](const auto& device) {
        if (device->supportsMetric(hal::power::PowerDevice::MetricType::ChargeLevel)) {
            power = device;
            return false;
        }
        return true;
    });

    if (power == nullptr) {
        return nullptr;
    }

    hal::power::PowerDevice::MetricData charge_level;
    if (!power->getMetric(hal::power::PowerDevice::MetricType::ChargeLevel, charge_level)) {
        return nullptr;
    }

    uint8_t charge = charge_level.valueAsUint8;

    if (charge >= 95) {
        return LVGL_SYMBOL_BATTERY_ANDROID_FRAME_FULL;
    } else if (charge >= 80) {
        return LVGL_SYMBOL_BATTERY_ANDROID_FRAME_6;
    } else if (charge >= 64) {
        return LVGL_SYMBOL_BATTERY_ANDROID_FRAME_5;
    } else if (charge >= 48) {
        return LVGL_SYMBOL_BATTERY_ANDROID_FRAME_4;
    } else if (charge >= 32) {
        return LVGL_SYMBOL_BATTERY_ANDROID_FRAME_3;
    } else if (charge >= 16) {
        return LVGL_SYMBOL_BATTERY_ANDROID_FRAME_2;
    } else  {
        return LVGL_SYMBOL_BATTERY_ANDROID_FRAME_1;
    }
}

class StatusbarService final : public Service {

    Mutex mutex;
    std::unique_ptr<Timer> updateTimer;
    int8_t gps_icon_id;
    bool gps_last_state = false;
    int8_t wifi_icon_id;
    const char* wifi_last_icon = nullptr;
    int8_t sdcard_icon_id;
    const char* sdcard_last_icon = nullptr;
    int8_t power_icon_id;
    const char* power_last_icon = nullptr;

    void lock() const {
        mutex.lock();
    }

    void unlock() const {
        mutex.unlock();
    }

    void updateGpsIcon() {
        auto gps_state = gps::findGpsService()->getState();
        bool show_icon = (gps_state == gps::State::OnPending) || (gps_state == gps::State::On);
        if (gps_last_state != show_icon) {
            if (show_icon) {
                lvgl::statusbar_icon_set_image(gps_icon_id, LVGL_SYMBOL_LOCATION_ON);
                lvgl::statusbar_icon_set_visibility(gps_icon_id, true);
            } else {
                lvgl::statusbar_icon_set_visibility(gps_icon_id, false);
            }
            gps_last_state = show_icon;
        }
    }

    void updateWifiIcon() {
        wifi::RadioState radio_state = wifi::getRadioState();
        const char* desired_icon = getWifiStatusIcon(radio_state);
        if (wifi_last_icon != desired_icon) {
            if (desired_icon != nullptr) {
                lvgl::statusbar_icon_set_image(wifi_icon_id, desired_icon);
                lvgl::statusbar_icon_set_visibility(wifi_icon_id, true);
            } else {
                lvgl::statusbar_icon_set_visibility(wifi_icon_id, false);
            }
            wifi_last_icon = desired_icon;
        }
    }

    void updatePowerStatusIcon() {
        const char* desired_icon = getPowerStatusIcon();
        if (power_last_icon != desired_icon) {
            if (desired_icon != nullptr) {
                lvgl::statusbar_icon_set_image(power_icon_id, desired_icon);
                lvgl::statusbar_icon_set_visibility(power_icon_id, true);
            } else {
                lvgl::statusbar_icon_set_visibility(power_icon_id, false);
            }
            power_last_icon = desired_icon;
        }
    }

    void updateSdCardIcon() {
        auto sdcards = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
        // TODO: Support multiple SD cards
        auto sdcard = sdcards.empty() ? nullptr : sdcards[0];
        if (sdcard != nullptr) {
            auto state = sdcard->getState(50 / portTICK_PERIOD_MS);
            if (state != hal::sdcard::SdCardDevice::State::Timeout) {
                auto* desired_icon = getSdCardStatusIcon(state);
                if (sdcard_last_icon != desired_icon) {
                    lvgl::statusbar_icon_set_image(sdcard_icon_id, desired_icon);
                    lvgl::statusbar_icon_set_visibility(sdcard_icon_id, true);
                    sdcard_last_icon = desired_icon;
                }
            }
            // TODO: Consider tracking how long the SD card has been in unknown status and then show error
        }
    }

    void update() {
        if (lvgl::isStarted()) {
            if (lvgl::lock(100)) {
                updateGpsIcon();
                updateWifiIcon();
                updateSdCardIcon();
                updatePowerStatusIcon();
                lvgl::unlock();
            }
        }
    }

public:

    StatusbarService() {
        gps_icon_id = lvgl::statusbar_icon_add();
        sdcard_icon_id = lvgl::statusbar_icon_add();
        wifi_icon_id = lvgl::statusbar_icon_add();
        power_icon_id = lvgl::statusbar_icon_add();
    }

    ~StatusbarService() override {
        lvgl::statusbar_icon_remove(wifi_icon_id);
        lvgl::statusbar_icon_remove(sdcard_icon_id);
        lvgl::statusbar_icon_remove(power_icon_id);
        lvgl::statusbar_icon_remove(gps_icon_id);
    }

    bool onStart(ServiceContext& serviceContext) override {
        if (lv_screen_active() == nullptr) {
            LOGGER.error("No display found");
            return false;
        }

        // TODO: Make thread-safe for LVGL
        lvgl::statusbar_icon_set_visibility(wifi_icon_id, true);

        auto service = findServiceById<StatusbarService>(manifest.id);
        assert(service);
        service->update();

        updateTimer = std::make_unique<Timer>(Timer::Type::Periodic, pdMS_TO_TICKS(1000), [service] {
            service->update();
        });

        updateTimer->setCallbackPriority(Thread::Priority::Lower);

        // We want to try and scan more often in case of startup or scan lock failure
        updateTimer->start();

        return true;
    }

    void onStop(ServiceContext& service) override{
        updateTimer->stop();
        updateTimer = nullptr;
    }
};

extern const ServiceManifest manifest = {
    .id = "Statusbar",
    .createService = create<StatusbarService>
};

// endregion service

} // namespace
