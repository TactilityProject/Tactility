#pragma once

#include <cstdint>

namespace tt::service::buttonactions {

enum class Action {
    None,
    /** Shows the system menu with the power options, or hides the menu when it is shown */
    PowerMenu,
    Standby,
    Suspend
};

/** @return what a button key code triggers */
Action getAction(uint16_t code);

} // namespace tt::service::buttonactions
