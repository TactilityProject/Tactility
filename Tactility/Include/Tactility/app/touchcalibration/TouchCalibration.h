#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED)

#include <Tactility/app/App.h>

namespace tt::app::touchcalibration {

LaunchId start();

} // namespace tt::app::touchcalibration

#endif
