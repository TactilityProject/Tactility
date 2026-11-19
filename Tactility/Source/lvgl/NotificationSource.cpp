#include <Tactility/lvgl/NotificationSource.h>

#include <app/manager.h>

#include <cstring>

namespace tt::lvgl {

::std::string getNotificationSourceName(const char* sourceId) {
    AppManifest manifest;
    if (app_manager_find_manifest(sourceId, &manifest) == ERROR_NONE && manifest.name[0] != '\0') {
        return manifest.name;
    }
    const char* lastDot = strrchr(sourceId, '.');
    return lastDot != nullptr ? lastDot + 1 : sourceId;
}

}
