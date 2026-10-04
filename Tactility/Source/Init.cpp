#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include <Tactility/InitEsp.h>
#endif

#include <Tactility/Tactility.h>
#include <Tactility/TactilityPrivate.h>

#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/file/File.h>
#include <Tactility/hal/SdCard.h>
#include <Tactility/network/NtpPrivate.h>
#include <Tactility/settings/TimePrivate.h>

#include <tactility/kernel_init.h>
#include <tactility/log.h>
#include <tactility/paths.h>

namespace tt {

constexpr auto* TAG = "Tactility";

void prepareFileSystems() {
    char temp_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_temp_path(temp_path, sizeof(temp_path)) != ERROR_NONE) {
        LOG_E(TAG, "Failed to determine temp path");
        return;
    }
    if (!file::findOrCreateDirectory(temp_path, 0777)) {
        LOG_E(TAG, "Failed to create %s", temp_path);
    }
}

bool init(Module* const dtsModules[], const DtsDevice dtsDevices[]) {
    LOG_I(TAG, "Tactility v%s on %s (%s)", TT_VERSION, CONFIG_TT_DEVICE_NAME, CONFIG_TT_DEVICE_ID);

    LOG_I(TAG, "Initializing kernel");
    if (kernel_init(dtsModules, dtsDevices) != ERROR_NONE) {
        LOG_E(TAG, "Failed to initialize kernel");
        return false;
    }

    initModules();

#ifdef ESP_PLATFORM
    initEsp();
#endif

    startBootApp();

    return true;
}

} // namespace
