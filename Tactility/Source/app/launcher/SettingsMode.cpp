#include <Tactility/app/launcher/LauncherMode.h>

#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl/icons/shared.h>

#include <algorithm>
#include <cstring>

namespace tt::app::launcher {

namespace {

struct IconEntry {
    const char* id;
    const char* icon;
};

constexpr IconEntry ICONS[] = {
    {"tactility.appearance",        LVGL_ICON_SHARED_PALETTE},
    {"tactility.apppackagelist",    LVGL_ICON_SHARED_DEPLOYED_CODE},
    {"tactility.audiosettings",     LVGL_ICON_SHARED_MUSIC_NOTE},
    {"tactility.btmanage",          LVGL_ICON_SHARED_BLUETOOTH},
    {"tactility.development",       LVGL_ICON_SHARED_LOGO_DEV},
    {"tactility.display",           LVGL_ICON_SHARED_DISPLAY_SETTINGS},
    {"tactility.gpssettings",       LVGL_ICON_SHARED_NAVIGATION},
    {"tactility.grovesettings",     LVGL_ICON_SHARED_CABLE},
    {"tactility.keyboardsettings",  LVGL_ICON_SHARED_KEYBOARD_ALT},
    {"tactility.ledstripsettings",  LVGL_ICON_SHARED_LIGHTSTRIP},
    {"tactility.localesettings",    LVGL_ICON_SHARED_LANGUAGE},
    {"tactility.power",             LVGL_ICON_SHARED_POWER_SETTINGS_NEW},
    {"tactility.timedatesettings",  LVGL_ICON_SHARED_CALENDAR_MONTH},
    {"tactility.touchcalibration",  LVGL_ICON_SHARED_CIRCLE},
    {"tactility.trackballsettings", LVGL_ICON_SHARED_DEVICES},
    {"tactility.usbsettings",       LVGL_ICON_SHARED_USB},
    {"tactility.wifimanage",        LVGL_ICON_SHARED_WIFI},
};

const char* appIcon(const ::AppManifest* manifest) {
    for (const auto& entry : ICONS) {
        if (!strcmp(manifest->id, entry.id)) return entry.icon;
    }
    return LVGL_ICON_SHARED_DEPLOYED_CODE;
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<::AppManifest>*>(context);
    manifests->push_back(*manifest);
}

std::vector<TileGridItem> collectItems() {
    std::vector<::AppManifest> collected;
    app_manager_for_each_manifest(collectManifest, &collected);
    std::ranges::sort(collected, [](const ::AppManifest& a, const ::AppManifest& b) {
        return strcmp(a.name, b.name) < 0;
    });

    std::vector<TileGridItem> items;
    for (const auto& manifest : collected) {
        if (manifest.category == APP_CATEGORY_SETTINGS && (manifest.flags & APP_MANIFEST_FLAG_HIDDEN) == 0) {
            items.push_back({ manifest.id, manifest.name, appIcon(&manifest), false });
        }
    }
    return items;
}

} // namespace

const LauncherMode SETTINGS_MODE = {
    .buttonIcon = LVGL_ICON_SHARED_SETTINGS,
    .iconColor = TileGrid::IconColor::Secondary,
    .collect = collectItems,
    .onLongPressed = nullptr,
    .onKey = nullptr
};

}
