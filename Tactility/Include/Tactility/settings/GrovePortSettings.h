#pragma once

#include <tactility/drivers/grove.h>

namespace tt::settings::grove {

/** Loads the saved mode of the Grove port with this device name. @return false when none was saved */
bool loadMode(const char* deviceName, GroveMode& mode);

/** Saves the mode of the Grove port with this device name, so it is restored at boot */
bool saveMode(const char* deviceName, GroveMode mode);

/** Sets every Grove port to its saved mode. Ports without a saved mode keep their devicetree default. */
void restoreAll();

} // namespace tt::settings::grove
