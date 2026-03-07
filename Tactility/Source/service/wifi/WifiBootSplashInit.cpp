#include <Tactility/service/wifi/WifiBootSplashInit.h>
#include <Tactility/file/PropertiesFile.h>

#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>
#include <Tactility/Logger.h>
#include <Tactility/service/wifi/WifiApSettings.h>

#include <Tactility/Paths.h>
#include <Tactility/Tactility.h>
#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <dirent.h>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace tt::service::wifi {

static const auto LOGGER = Logger("WifiBootSplashInit");

constexpr auto* AP_PROPERTIES_KEY_SSID = "ssid";
constexpr auto* AP_PROPERTIES_KEY_PASSWORD = "password";
constexpr auto* AP_PROPERTIES_KEY_AUTO_CONNECT = "autoConnect";
constexpr auto* AP_PROPERTIES_KEY_CHANNEL = "channel";
constexpr auto* AP_PROPERTIES_KEY_AUTO_REMOVE = "autoRemovePropertiesFile";

struct ApProperties {
    std::string ssid;
    std::string password;
    bool autoConnect;
    int32_t channel;
    bool autoRemovePropertiesFile;
};

static void importWifiAp(const std::string& filePath) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(filePath, map)) {
        LOGGER.error("Failed to load AP properties at {}", filePath);
        return;
    }

    const auto ssid_iterator = map.find(AP_PROPERTIES_KEY_SSID);
    if (ssid_iterator == map.end()) {
        LOGGER.error("{} is missing ssid", filePath);
        return;
    }
    const auto ssid = ssid_iterator->second;

    if (!settings::contains(ssid)) {

        const auto password_iterator = map.find(AP_PROPERTIES_KEY_PASSWORD);
        const auto password = password_iterator == map.end() ? "" : password_iterator->second;

        const auto auto_connect_iterator = map.find(AP_PROPERTIES_KEY_AUTO_CONNECT);
        const auto auto_connect = auto_connect_iterator == map.end() ? true : (auto_connect_iterator->second == "true");

        const auto channel_iterator = map.find(AP_PROPERTIES_KEY_CHANNEL);
        const auto channel = channel_iterator == map.end() ? 0 : std::stoi(channel_iterator->second);

        settings::WifiApSettings settings(
            ssid,
            password,
            auto_connect,
            channel
        );

        if (!settings::save(settings)) {
            LOGGER.error("Failed to save settings for {}", ssid);
        } else {
            LOGGER.info("Imported {} from {}", ssid, filePath);
        }
    }

    const auto auto_remove_iterator = map.find(AP_PROPERTIES_KEY_AUTO_REMOVE);
    if (auto_remove_iterator != map.end() && auto_remove_iterator->second == "true") {
        if (!remove(filePath.c_str())) {
            LOGGER.error("Failed to auto-remove {}", filePath);
        } else {
            LOGGER.info("Auto-removed {}", filePath);
        }
    }
}

static void importWifiApSettingsFromDir(const std::string& path) {
    std::vector<dirent> dirent_list;
    if (file::scandir(path, dirent_list, [](const dirent* entry) {
        switch (entry->d_type) {
            case file::TT_DT_DIR:
            case file::TT_DT_CHR:
            case file::TT_DT_LNK:
                return -1;
            case file::TT_DT_REG:
            default: {
                std::string name = entry->d_name;
                if (name.ends_with(".ap.properties")) {
                    return 0;
                } else {
                    return -1;
                }
            }
        }
    }, nullptr) == 0) {
        // keep original behavior: if scandir returns 0, give up silently
        return;
    }

    if (dirent_list.empty()) {
        LOGGER.warn("No AP files found at {}", path);
        return;
    }

    for (auto& dirent : dirent_list) {
        std::string absolute_path = std::format("{}/{}", path, dirent.d_name);
        importWifiAp(absolute_path);
    }
}

void bootSplashInit() {
    getMainDispatcher().dispatch([] {
        // First import any provisioning files placed on the system data partition.
        const std::string data_settings_path = file::getChildPath(file::MOUNT_POINT_DATA, "settings");
        importWifiApSettingsFromDir(data_settings_path);

        // Then scan attached SD cards as before.
        std::string sdcard_path;
        if (findFirstMountedSdCardPath((sdcard_path))) {
            const std::string sd_settings_path = file::getChildPath(sdcard_path, "settings");
            importWifiApSettingsFromDir(sd_settings_path);
        }
    });
}

}
