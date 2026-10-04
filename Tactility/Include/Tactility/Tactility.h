#pragma once

#include <Tactility/MainDispatcher.h> // Included for backwards compatibility

#include <tactility/device.h>
#include <tactility/module.h>

extern "C" {
struct DtsDevice;
}

namespace tt {

/**
 * @brief Initializes Tactility and starts the Boot app.
 * @param dtsModules List of modules from devicetree, null-terminated, non-null parameter
 * @param dtsDevices Array that is terminated with DTS_DEVICE_TERMINATOR
 * @return false when the kernel failed to initialize
 */
bool init(Module* const dtsModules[], const DtsDevice dtsDevices[]);

} // namespace tt
