#include <Tactility/settings/LauncherSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <tactility/paths.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <map>
#include <string>

namespace tt::settings::launcher {

constexpr auto* SETTINGS_KEY_SYSTEM_BARS = "systemBars";
constexpr auto* SYSTEM_BARS_AUTO = "auto";
constexpr auto* SYSTEM_BARS_CUSTOM = "custom";
constexpr auto* SETTINGS_KEY_PORTRAIT_LAYOUT = "portraitLayout";
constexpr auto* SETTINGS_KEY_LANDSCAPE_LAYOUT = "landscapeLayout";
constexpr auto* LAYOUT_SPLIT = "split";
constexpr auto* LAYOUT_SIDE = "side";

static const char* toString(SystemBarsLayout layout) {
    return layout == SystemBarsLayout::Side ? LAYOUT_SIDE : LAYOUT_SPLIT;
}

static SystemBarsLayout toLayout(const std::map<std::string, std::string>& map, const char* key, SystemBarsLayout defaultValue) {
    auto entry = map.find(key);
    if (entry == map.end()) {
        return defaultValue;
    }
    if (entry->second == LAYOUT_SIDE) {
        return SystemBarsLayout::Side;
    }
    if (entry->second == LAYOUT_SPLIT) {
        return SystemBarsLayout::Split;
    }
    return defaultValue;
}

static bool getSettingsFilePath(std::string& outPath) {
    char root[128];
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    outPath = std::string(root) + "/settings/launcher.properties";
    return true;
}

bool load(LauncherSettings& settings) {
    std::string settings_path;
    if (!getSettingsFilePath(settings_path) || !file::isFile(settings_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
        return false;
    }

    settings = getDefault();
    auto system_bars = map.find(SETTINGS_KEY_SYSTEM_BARS);
    if (system_bars != map.end() && system_bars->second == SYSTEM_BARS_CUSTOM) {
        settings.systemBarsMode = SystemBarsMode::Custom;
    }
    settings.portraitLayout = toLayout(map, SETTINGS_KEY_PORTRAIT_LAYOUT, settings.portraitLayout);
    settings.landscapeLayout = toLayout(map, SETTINGS_KEY_LANDSCAPE_LAYOUT, settings.landscapeLayout);
    return true;
}

LauncherSettings getDefault() {
    return LauncherSettings {
        .systemBarsMode = SystemBarsMode::Auto,
        .portraitLayout = SystemBarsLayout::Split,
        .landscapeLayout = SystemBarsLayout::Side
    };
}

LauncherSettings loadOrGetDefault() {
    LauncherSettings settings;
    if (!load(settings)) {
        settings = getDefault();
    }
    return settings;
}

bool save(const LauncherSettings& settings) {
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_SYSTEM_BARS] = settings.systemBarsMode == SystemBarsMode::Custom ? SYSTEM_BARS_CUSTOM : SYSTEM_BARS_AUTO;
    map[SETTINGS_KEY_PORTRAIT_LAYOUT] = toString(settings.portraitLayout);
    map[SETTINGS_KEY_LANDSCAPE_LAYOUT] = toString(settings.landscapeLayout);

    std::string settings_path;
    if (!getSettingsFilePath(settings_path)) {
        return false;
    }

    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        return false;
    }

    return file::savePropertiesFile(settings_path, map);
}

SystemBarsCapabilities getSystemBarsCapabilities() {
    SystemBarsCapabilities capabilities = { SystemBarsCapability::Any, SystemBarsCapability::Any };
#if defined(CONFIG_TT_SYSTEM_BARS_PORTRAIT_SPLIT)
    capabilities.portrait = SystemBarsCapability::Split;
#elif defined(CONFIG_TT_SYSTEM_BARS_PORTRAIT_SIDE)
    capabilities.portrait = SystemBarsCapability::Side;
#endif
#if defined(CONFIG_TT_SYSTEM_BARS_LANDSCAPE_SPLIT)
    capabilities.landscape = SystemBarsCapability::Split;
#elif defined(CONFIG_TT_SYSTEM_BARS_LANDSCAPE_SIDE)
    capabilities.landscape = SystemBarsCapability::Side;
#endif
    return capabilities;
}

SystemBarsLayout resolveSystemBarsLayout(const LauncherSettings& settings, const SystemBarsCapabilities& capabilities, int32_t width, int32_t height) {
    const bool landscape = width > height;
    const auto capability = landscape ? capabilities.landscape : capabilities.portrait;
    if (capability == SystemBarsCapability::Split) {
        return SystemBarsLayout::Split;
    }
    if (capability == SystemBarsCapability::Side) {
        return SystemBarsLayout::Side;
    }
    if (settings.systemBarsMode == SystemBarsMode::Custom) {
        return landscape ? settings.landscapeLayout : settings.portraitLayout;
    }
    // Only wide landscape screens: an aspect ratio of 1.5 or more
    return (landscape && (width * 2 >= height * 3)) ? SystemBarsLayout::Side : SystemBarsLayout::Split;
}

} // namespace tt::settings::launcher
