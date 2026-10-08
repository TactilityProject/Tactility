#include <Tactility/app/launcher/Favourites.h>
#include <Tactility/app/launcher/LauncherMode.h>

#include <app/manager.h>
#include <app/manifest.h>
#include <app/package_manifest.h>

#include <lvgl/icons/shared.h>
#include <tactility/log.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace tt::app::launcher {

namespace {

constexpr auto* TAG = "Launcher";

const Favourites favourites;

struct IconEntry {
    const char* id;
    const char* icon;
};

constexpr IconEntry ICONS[] = {
    {"tactility.apphub",            LVGL_ICON_SHARED_DOWNLOAD},
    {"tactility.camera",            LVGL_ICON_SHARED_CAMERA},
    {"tactility.chat",              LVGL_ICON_SHARED_FORUM},
    {"tactility.files",             LVGL_ICON_SHARED_FOLDER},
    {"tactility.i2cscanner",        LVGL_ICON_SHARED_CABLE},
    {"tactility.notes",             LVGL_ICON_SHARED_EDIT_NOTE},
    {"tactility.screenshot",        LVGL_ICON_SHARED_IMAGE},
    {"tactility.systeminfo",        LVGL_ICON_SHARED_DEVICES},
    {"tactility.terminal",          LVGL_ICON_SHARED_TERMINAL},
    {"tactility.webserversettings", LVGL_ICON_SHARED_CLOUD},
};

// Hidden apps that are still shown in the launcher
constexpr const char* SHOWN_HIDDEN_APP_IDS[] = {
    "tactility.files",
};

const char* appIcon(const ::AppManifest* manifest) {
    for (const auto& entry : ICONS) {
        if (!strcmp(manifest->id, entry.id)) return entry.icon;
    }
    return LVGL_ICON_SHARED_DEPLOYED_CODE;
}

bool isShown(const ::AppManifest& manifest) {
    if (manifest.category != APP_CATEGORY_USER && manifest.category != APP_CATEGORY_SYSTEM) {
        return false;
    }
    if ((manifest.flags & APP_MANIFEST_FLAG_HIDDEN) == 0) {
        return true;
    }
    return std::ranges::any_of(SHOWN_HIDDEN_APP_IDS, [&](const char* id) { return strcmp(manifest.id, id) == 0; });
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<::AppManifest>*>(context);
    manifests->push_back(*manifest);
}

// Apps from installed packages this device can't run (wrong device, too little RAM)
void collectIncompatibleAppIds(const AppPackage* package, void* context) {
    if (app_package_manifest_is_compatible(&package->package)) {
        return;
    }
    auto* ids = static_cast<std::vector<std::string>*>(context);
    for (size_t i = 0; i < package->app_id_count; i++) {
        ids->emplace_back(package->app_ids[i]);
    }
}

std::vector<AppGridItem> collectItems() {
    const std::vector<std::string> favouriteIds = favourites.load();

    std::vector<::AppManifest> collected;
    app_manager_for_each_manifest(collectManifest, &collected);
    std::vector<std::string> incompatibleIds;
    app_manager_for_each_package(collectIncompatibleAppIds, &incompatibleIds);
    std::erase_if(collected, [&](const ::AppManifest& manifest) {
        return !isShown(manifest) || std::ranges::find(incompatibleIds, std::string(manifest.id)) != incompatibleIds.end();
    });

    std::vector<AppGridItem> items;
    items.reserve(collected.size());
    for (const auto& manifest : collected) {
        items.push_back({ manifest, appIcon(&manifest), Favourites::contains(favouriteIds, manifest.id) });
    }
    std::ranges::sort(items, [](const AppGridItem& a, const AppGridItem& b) {
        if (a.highlighted != b.highlighted) {
            return a.highlighted;
        }
        return strcmp(a.manifest.name, b.manifest.name) < 0;
    });
    return items;
}

void toggleFavourite(AppGrid& grid, const char* appId, bool keepSelection) {
    if (!favourites.toggle(appId)) {
        LOG_E(TAG, "Failed to save favourites");
    }
    grid.requestRepopulate(keepSelection);
}

void onLongPressed(AppGrid& grid, const ::AppManifest& manifest) {
    toggleFavourite(grid, manifest.id, false);
}

void onKey(AppGrid& grid, const ::AppManifest& manifest, uint32_t key) {
    if (key == 'f' || key == 'F') {
        toggleFavourite(grid, manifest.id, true);
    }
}

} // namespace

const LauncherMode APPS_MODE = {
    .buttonIcon = LVGL_ICON_SHARED_APPS,
    .iconColor = AppGrid::IconColor::Primary,
    .collect = collectItems,
    .onLongPressed = onLongPressed,
    .onKey = onKey
};

}
