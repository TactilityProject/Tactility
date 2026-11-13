#include <Tactility/app/launcher/LauncherMode.h>

#include <app/manager.h>
#include <app/manifest.h>

#include <generated_assets.h>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/svg.h>
#include <lvgl/theme.h>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/Memory.hpp>

#include <algorithm>
#include <cstring>
#include <strings.h>

namespace tt::app::launcher {

namespace {

struct SettingsIconAsset {
    const char* id;
    const uint8_t* colour;
    size_t colourSize;
    const uint8_t* mono;
    size_t monoSize;
};

#define SETTINGS_ICON(id, name) { id, asset_app_icon_##name##_svg, asset_app_icon_##name##_svg_SIZE, asset_app_icon_##name##_mono_svg, asset_app_icon_##name##_mono_svg_SIZE }

const SettingsIconAsset SETTINGS_ICONS[] = {
    SETTINGS_ICON("tactility.appearance", tactility_appearance),
    SETTINGS_ICON("tactility.audiosettings", tactility_audiosettings),
    SETTINGS_ICON("tactility.btmanage", tactility_btmanage),
    SETTINGS_ICON("tactility.development", tactility_development),
    SETTINGS_ICON("tactility.display", tactility_display),
    SETTINGS_ICON("tactility.grovesettings", tactility_grovesettings),
    SETTINGS_ICON("tactility.keyboardsettings", tactility_keyboardsettings),
    SETTINGS_ICON("tactility.ledstripsettings", tactility_ledstripsettings),
    SETTINGS_ICON("tactility.locationsettings", tactility_locationsettings),
    SETTINGS_ICON("tactility.meshsettings", tactility_meshsettings),
    SETTINGS_ICON("tactility.power", tactility_power),
    SETTINGS_ICON("tactility.timedatesettings", tactility_timedatesettings),
    SETTINGS_ICON("tactility.touchcalibration", tactility_touchcalibration),
    SETTINGS_ICON("tactility.trackballsettings", tactility_trackballsettings),
    SETTINGS_ICON("tactility.usbsettings", tactility_usbsettings),
    SETTINGS_ICON("tactility.vibrationsettings", tactility_vibrationsettings),
    SETTINGS_ICON("tactility.wifimanage", tactility_wifimanage),
};

constexpr size_t SETTINGS_ICON_COUNT = sizeof(SETTINGS_ICONS) / sizeof(SETTINGS_ICONS[0]);

// Written by loadSettingsIcons() on the file operations task, read when collecting items on the LVGL thread
struct SettingsIcons {
    Mutex mutex;
    lv_draw_buf_t* images[SETTINGS_ICON_COUNT] = {};
    // Mono icons are painted in the theme's icon colour
    bool recolor[SETTINGS_ICON_COUNT] = {};
    bool loaded = false;
    bool forMono = false;
    // Replaced images may still be shown by tiles, so they are freed by the next load
    lv_draw_buf_t* retired[SETTINGS_ICON_COUNT] = {};

    SettingsIcons() { mutex_construct(&mutex); }
};

SettingsIcons& settingsIcons() {
    static SettingsIcons* instance = newObject<SettingsIcons>();
    check(instance != nullptr);
    return *instance;
}

// Returns the default icon for apps without one, or nullptr until the icons are loaded
const lv_image_dsc_t* settingsIcon(const char* appId, bool* recolor) {
    auto& state = settingsIcons();
    mutex_lock(&state.mutex);
    const bool loaded = state.loaded;
    const lv_image_dsc_t* image = nullptr;
    if (loaded) {
        for (size_t i = 0; i < SETTINGS_ICON_COUNT; i++) {
            if (!strcmp(appId, SETTINGS_ICONS[i].id)) {
                image = reinterpret_cast<const lv_image_dsc_t*>(state.images[i]);
                *recolor = state.recolor[i];
                break;
            }
        }
    }
    mutex_unlock(&state.mutex);
    if (loaded && image == nullptr) {
        image = defaultAppIcon(recolor);
    }
    return image;
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<::AppManifest>*>(context);
    manifests->push_back(*manifest);
}

std::vector<TileGridItem> collectItems() {
    std::vector<::AppManifest> collected;
    app_manager_for_each_manifest(collectManifest, &collected);
    std::ranges::sort(collected, [](const ::AppManifest& a, const ::AppManifest& b) {
        const int nameOrder = strcasecmp(a.name, b.name);
        if (nameOrder != 0) {
            return nameOrder < 0;
        }
        return strcmp(a.id, b.id) < 0;
    });

    std::vector<TileGridItem> items;
    for (const auto& manifest : collected) {
        if (manifest.category == APP_CATEGORY_SETTINGS && (manifest.flags & APP_MANIFEST_FLAG_HIDDEN) == 0) {
            // The glyph is shown until the icons are loaded
            bool recolor = false;
            const lv_image_dsc_t* image = settingsIcon(manifest.id, &recolor);
            items.push_back({ manifest.id, manifest.name, LVGL_ICON_SHARED_DEPLOYED_CODE, false, image, recolor });
        }
    }
    return items;
}

} // namespace

void loadSettingsIcons(void*) {
    auto& state = settingsIcons();
    const bool preferMono = lvgl_theme_is_mono();
    // Only this task writes the icons, so reading them here needs no lock
    if (state.loaded && state.forMono == preferMono) return;

    // Replaced images are freed now, as the tiles that showed them were rebuilt since
    for (auto*& image : state.retired) {
        if (image != nullptr) lv_draw_buf_destroy(image);
        image = nullptr;
    }

    const auto size = static_cast<uint32_t>(lvgl_get_shared_icon_large_font_height());
    lv_draw_buf_t* images[SETTINGS_ICON_COUNT] = {};
    bool recolor[SETTINGS_ICON_COUNT] = {};
    for (size_t i = 0; i < SETTINGS_ICON_COUNT; i++) {
        const auto& asset = SETTINGS_ICONS[i];
        images[i] = lvgl_svg_rasterize_variants(
            reinterpret_cast<const char*>(asset.colour), asset.colourSize,
            reinterpret_cast<const char*>(asset.mono), asset.monoSize,
            preferMono, size, &recolor[i]
        );
    }

    mutex_lock(&state.mutex);
    for (size_t i = 0; i < SETTINGS_ICON_COUNT; i++) {
        state.retired[i] = state.images[i];
        state.images[i] = images[i];
        state.recolor[i] = recolor[i];
    }
    state.loaded = true;
    state.forMono = preferMono;
    mutex_unlock(&state.mutex);
}

bool areSettingsIconsLoaded() {
    auto& state = settingsIcons();
    mutex_lock(&state.mutex);
    const bool loaded = state.loaded;
    mutex_unlock(&state.mutex);
    return loaded;
}

bool areSettingsIconsCurrent() {
    auto& state = settingsIcons();
    mutex_lock(&state.mutex);
    const bool current = state.loaded && state.forMono == lvgl_theme_is_mono();
    mutex_unlock(&state.mutex);
    return current;
}

const LauncherMode SETTINGS_MODE = {
    .buttonIcon = LVGL_ICON_SHARED_SETTINGS,
    .iconColor = TileGrid::IconColor::Secondary,
    .collect = collectItems,
    .onLongPressed = nullptr,
    .onKey = nullptr
};

}
