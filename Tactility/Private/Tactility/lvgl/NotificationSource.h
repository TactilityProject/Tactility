#pragma once

#include <string>

namespace tt::lvgl {

/**
 * The name to show for the app or service that sent a notification. A service is named after the app that has its id,
 * as the mesh service and the Mesh app do. Without one, the last part of the id is shown.
 */
::std::string getNotificationSourceName(const char* sourceId);

}
