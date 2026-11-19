#pragma once

#include <Tactility/settings/StandbySettings.h>

#include <string>
#include <vector>

namespace tt::standby {

/** The kind of driver that an input device uses, which decides how it is read */
enum class WakeInput {
    Keyboard,
    Touch,
    Button
};

/** An input device that can wake the device */
struct WakeSource {
    ::std::string name;
    settings::standby::WakeKind kind;
    WakeInput input;
};

/** @return true while the device is in standby. Input of that time is not an action. */
bool isActive();

/** @return the keyboards, touch screens, encoders and buttons that are ready */
::std::vector<WakeSource> findWakeSources();

/** @return the wake sources that the settings enable */
::std::vector<WakeSource> findEnabledWakeSources(const settings::standby::StandbySettings& settings);

/**
 * Turns off what the settings don't keep awake, stops LVGL, turns the display off and blocks until an enabled wake source is used.
 * Then it restores everything. Blocks, so call it from the task of an app and not from the LVGL thread.
 * @retval false when no wake source is enabled, in which case nothing happens
 */
bool enter(const settings::standby::StandbySettings& settings);

} // namespace tt::standby
