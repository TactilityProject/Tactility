#pragma once

namespace tt::app::shutdown {

/** The values of the argument of the "shutdown" app */
constexpr auto* MODE_STANDBY = "standby";
constexpr auto* MODE_SUSPEND = "suspend";
constexpr auto* MODE_POWER_OFF = "poweroff";
/** Shows the system menu with the power options, or hides the system menu when it is shown */
constexpr auto* MODE_MENU = "menu";

/** @return true when a power supply can power off the device */
bool supportsPowerOff();

/** @return true when a sleep controller can suspend the device */
bool supportsSuspend();

/** @return true when there is an input device that can wake the device from standby */
bool supportsStandby();

/** Starts the "shutdown" app with the mode, which is one of the MODE_* values. */
void start(const char* mode);

} // namespace tt::app::shutdown
