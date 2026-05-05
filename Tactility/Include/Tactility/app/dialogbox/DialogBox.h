#pragma once

#include <Tactility/Bundle.h>

#include <cstdint>
#include <string>
#include <Tactility/app/App.h>

namespace tt::app::dialogbox {

/**
 * Show a dialog box with the provided title and message, auto-dismissed after timeoutMs.
 * The dialog renders on the highest z-order and can be dismissed by touch or timeout.
 * @param[in] title the title to display at the top of the dialog box
 * @param[in] message the message to display
 * @param[in] timeoutMs auto-dismiss timeout in milliseconds, 0 for touch-only
 * @return the launch id
 */
LaunchId start(const std::string& title, const std::string& message, uint32_t timeoutMs);

/**
 * Show a dialog box with the provided title and message.
 * The dialog renders on the highest z-order and is dismissed by touch.
 * @param[in] title the title to display at the top of the dialog box
 * @param[in] message the message to display
 * @return the launch id
 */
LaunchId start(const std::string& title, const std::string& message);

/**
 * Show a dialog box with only a message (no title), auto-dismissed after timeoutMs.
 * The dialog renders on the highest z-order and can be dismissed by touch or timeout.
 * @param[in] message the message to display
 * @param[in] timeoutMs auto-dismiss timeout in milliseconds, 0 for touch-only
 * @return the launch id
 */
LaunchId start(const std::string& message, uint32_t timeoutMs);

/**
 * Show a dialog box with only a message (no title).
 * The dialog renders on the highest z-order and is dismissed by touch.
 * @param[in] message the message to display
 * @return the launch id
 */
LaunchId start(const std::string& message);

}
