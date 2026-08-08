#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED)

#include <cstdint>

namespace tt::app::touchcalibration {

/**
 * Starts calibration as a modal child of @a callerAppInstanceId. Result (Ok=0/Error=2, no
 * bundle) is delivered as APP_EVENT_RESULT once the user dismisses the outcome screen.
 * @return the new app instance id
 */
uint32_t start(uint32_t callerAppInstanceId);

} // namespace tt::app::touchcalibration

#endif
