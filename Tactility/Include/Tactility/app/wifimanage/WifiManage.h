#pragma once

#include <cstdint>

namespace tt::app::wifimanage {

/**
 * Starts as a modal child of @a callerAppInstanceId (see app_manager_start_for_result()) - an
 * APP_EVENT_RESULT is delivered back once the user closes this screen (default Cancelled/no
 * bundle if never explicitly set - callers that just want a "the wifi step is done" signal, like
 * Setup, can ignore the actual result value).
 * @return the new app instance id
 */
uint32_t start(uint32_t callerAppInstanceId);

} // namespace
