#include <Tactility/service/buttonactions/ButtonActions.h>

#include <Tactility/app/shutdown/Shutdown.h>
#include <Tactility/standby/Standby.h>

#include <app/manager.h>
#include <service/manager.h>
#include <service/manifest.h>

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

void onButtonEvent(Device* device, const ButtonEvent* event, void* /*context*/) {
    // A release ends the wake gesture, also while the end of standby is still being recovered
    if (event->value == BUTTON_VALUE_RELEASED) {
        standby::endWakeGesture(device);
        return;
    }
    // Input during standby wakes the device and is not an action
    if (standby::isActive()) {
        return;
    }
    // The press and long press of the gesture that woke the device, which can come after standby ended
    if (standby::isWakeGesture(device)) {
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

void* createService(const ::ServiceManifest* /*manifest*/) {
    return nullptr;
}

void destroyService(const ::ServiceManifest* /*manifest*/, void* /*data*/) {}

error_t onStart(::ServiceInstance* /*instance*/, void* /*data*/) {
    const error_t error = button_callback_add(onButtonEvent, nullptr);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to listen to buttons: %s", error_to_string(error));
    }
    return error;
}

void onStop(::ServiceInstance* /*instance*/, void* /*data*/) {
    button_callback_remove(onButtonEvent);
}

}

/** Runs the shutdown app for the power, standby and suspend keys of the button devices */
extern const ::ServiceManifest manifest = {
    .id = "tactility.buttonactions",
    .create_service = createService,
    .destroy_service = destroyService,
    .on_start = onStart,
    .on_stop = onStop
};

} // namespace tt::service::buttonactions
