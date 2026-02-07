#pragma once

#include "Tactility/hal/gps/GpsDevice.h"

struct Device;

namespace tt::hal::gps {

/**
 * Init sequence on UART for a specific GPS model.
 */
bool init(::Device* uart, GpsModel type);

}
