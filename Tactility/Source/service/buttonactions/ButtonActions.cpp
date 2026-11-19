#include <Tactility/service/buttonactions/ButtonActions.h>

#include <Tactility/app/shutdown/Shutdown.h>
#include <Tactility/service/Service.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/standby/Standby.h>

#include <app/manager.h>

#include <tactility/drivers/button.h>
#include <tactility/drivers/input_codes.h>
#include <tactility/log.h>

namespace tt::service::buttonactions {

constexpr auto* TAG = "ButtonActions";
constexpr auto* SHUTDOWN_APP_ID = "shutdown";

Action getAction(uint16_t code) {
    switch (code) {
        case BUTTON_KEY_POWER: return Action::PowerMenu;
        case BUTTON_KEY_STANDBY: return Action::Standby;
        case BUTTON_KEY_SUSPEND: return Action::Suspend;
        default: return Action::None;
    }
}

namespace {

/** The actions are done by the shutdown app, as the callback runs on the task of the button driver and must not block */
void startShutdown(const char* mode) {
    if (!app_manager_is_running(SHUTDOWN_APP_ID)) {
        app::shutdown::start(mode);
    }
}

void onButtonEvent(Device* /*device*/, const ButtonEvent* event, void* /*context*/) {
    // Input during standby wakes the device and is not an action
    if (event->value != BUTTON_VALUE_PRESSED || standby::isActive()) {
        return;
    }
    switch (getAction(event->code)) {
        case Action::PowerMenu:
            startShutdown(app::shutdown::MODE_MENU);
            break;
        case Action::Standby:
            startShutdown(app::shutdown::MODE_STANDBY);
            break;
        case Action::Suspend:
            startShutdown(app::shutdown::MODE_SUSPEND);
            break;
        case Action::None:
            break;
    }
}

}

/** Runs the shutdown app for the power, standby and suspend keys of the button devices */
class ButtonActionsService final : public Service {

public:

    bool onStart(ServiceContext& /*service*/) override {
        const error_t error = button_callback_add(onButtonEvent, nullptr);
        if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to listen to buttons: %s", error_to_string(error));
        }
        return error == ERROR_NONE;
    }

    void onStop(ServiceContext& /*service*/) override {
        button_callback_remove(onButtonEvent);
    }
};

extern const ServiceManifest manifest = {
    .id = "tactility.buttonactions",
    .createService = create<ButtonActionsService>
};

} // namespace tt::service::buttonactions
