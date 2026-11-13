#include <Tactility/app/launcher/Favourites.h>
#include <Tactility/app/launcher/LauncherMode.h>

#include <generated_assets.h>

#include <app/paths.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/package_manifest.h>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/svg.h>
#include <lvgl/theme.h>
#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/file_ops.h>
#include <tactility/log.h>
#include <tactility/Memory.hpp>
#include <tactility/memory.h>

#include <algorithm>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <new>
#include <string>

namespace tt::app::launcher {

namespace {

constexpr auto* TAG = "Launcher";

const Favourites favourites;

struct SaveFavouritesJob {
    std::vector<std::string> ids;
};

// File operation: runs on the file operations task, which frees the job
void saveFavourites(void* context) {
    auto* job = static_cast<SaveFavouritesJob*>(context);
    if (!favourites.save(job->ids)) {
        LOG_E(TAG, "Failed to save favourites");
    }
    delete job;
}

struct BuiltInIconAsset {
    const char* id;
    const uint8_t* colour;
    size_t colourSize;
    const uint8_t* mono;
    size_t monoSize;
};

#define BUILT_IN_ICON(id, name) { id, asset_app_icon_##name##_svg, asset_app_icon_##name##_svg_SIZE, asset_app_icon_##name##_mono_svg, asset_app_icon_##name##_mono_svg_SIZE }

const BuiltInIconAsset BUILT_IN_ICONS[] = {
    BUILT_IN_ICON("tactility.apphub", tactility_apphub),
    BUILT_IN_ICON("tactility.camera", tactility_camera),
    BUILT_IN_ICON("tactility.chat", tactility_chat),
    BUILT_IN_ICON("tactility.files", tactility_files),
    BUILT_IN_ICON("tactility.i2cscanner", tactility_i2cscanner),
    BUILT_IN_ICON("tactility.maps", tactility_maps),
    BUILT_IN_ICON("tactility.meshchannels", tactility_meshchannels),
    BUILT_IN_ICON("tactility.meshcontacts", tactility_meshcontacts),
    BUILT_IN_ICON("tactility.notes", tactility_notes),
    BUILT_IN_ICON("tactility.systeminfo", tactility_systeminfo),
    BUILT_IN_ICON("tactility.terminal", tactility_terminal),
};

const BuiltInIconAsset DEFAULT_ICON = BUILT_IN_ICON("", default);

constexpr auto* PACKAGE_ICON_FILE = "icon.svg";
constexpr auto* PACKAGE_ICON_MONO_FILE = "icon_mono.svg";

// What was found for one icon file, to detect that it was added, removed or changed
struct IconFileStamp {
    bool exists = false;
    time_t modified = 0;

    bool operator==(const IconFileStamp&) const = default;
};

struct PackageIcon {
    std::string packageId;
    std::vector<std::string> appIds;
    IconFileStamp colourFile;
    IconFileStamp monoFile;
    lv_draw_buf_t* image;
    // Mono icons are painted in the theme's icon colour
    bool recolor;
    // The theme the variant was picked for
    bool forMono;
};

// Written by loadPackageIcons() on the file operations task, read when collecting items on the LVGL thread
struct PackageIcons {
    Mutex mutex;
    std::vector<PackageIcon> icons;
    // The icons of built-in apps and the one for apps without an icon
    std::vector<PackageIcon> builtIn;
    PackageIcon defaultIcon {};
    bool builtInLoaded = false;
    bool builtInForMono = false;
    // Replaced images may still be shown by tiles, so they are freed when the grid has deleted its tiles, see releaseRetiredAppIcons()
    std::vector<lv_draw_buf_t*> retired;

    PackageIcons() { mutex_construct(&mutex); }
};

PackageIcons& packageIcons() {
    static PackageIcons* instance = newObject<PackageIcons>();
    check(instance != nullptr);
    return *instance;
}

const lv_image_dsc_t* toImageDsc(const lv_draw_buf_t* image) {
    return reinterpret_cast<const lv_image_dsc_t*>(image);
}

const lv_image_dsc_t* packageIcon(const char* appId, bool* recolor) {
    auto& state = packageIcons();
    const lv_image_dsc_t* image = nullptr;
    mutex_lock(&state.mutex);
    for (const auto& icon : state.icons) {
        if (::std::ranges::find(icon.appIds, appId) != icon.appIds.end()) {
            image = toImageDsc(icon.image);
            *recolor = icon.recolor;
            break;
        }
    }
    mutex_unlock(&state.mutex);
    return image;
}

// Image of a built-in app, or the default one
const lv_image_dsc_t* builtInIcon(const char* appId, bool* recolor) {
    auto& state = packageIcons();
    const lv_image_dsc_t* image = nullptr;
    mutex_lock(&state.mutex);
    const PackageIcon* found = &state.defaultIcon;
    for (const auto& icon : state.builtIn) {
        if (icon.packageId == appId) {
            found = &icon;
            break;
        }
    }
    image = toImageDsc(found->image);
    *recolor = found->recolor;
    mutex_unlock(&state.mutex);
    return image;
}

bool isShown(const ::AppManifest& manifest) {
    if (manifest.category != APP_CATEGORY_USER && manifest.category != APP_CATEGORY_SYSTEM) {
        return false;
    }
    if ((manifest.flags & APP_MANIFEST_FLAG_HEADLESS) != 0) {
        return false;
    }
    if ((manifest.flags & APP_MANIFEST_FLAG_HIDDEN) != 0) {
        return false;
    }

    return true;
}

void collectManifest(const ::AppManifest* manifest, void* context) {
    auto* manifests = static_cast<std::vector<::AppManifest>*>(context);
    manifests->push_back(*manifest);
}

// Apps from installed packages this device can't run (wrong device, too little RAM)
void collectIncompatibleAppIds(const AppPackage* package, void* context) {
    if (package->compatible) {
        return;
    }
    auto* ids = static_cast<std::vector<std::string>*>(context);
    for (size_t i = 0; i < package->app_id_count; i++) {
        ids->emplace_back(package->app_ids[i]);
    }
}

std::vector<TileGridItem> collectItems() {
    const std::vector<std::string> favouriteIds = favourites.ids();

    std::vector<::AppManifest> collected;
    app_manager_for_each_manifest(collectManifest, &collected);
    std::vector<std::string> incompatibleIds;
    app_manager_for_each_package(collectIncompatibleAppIds, &incompatibleIds);
    ::std::erase_if(collected, [&](const ::AppManifest& manifest) {
        return !isShown(manifest) || ::std::ranges::find(incompatibleIds, std::string(manifest.id)) != incompatibleIds.end();
    });

    const bool svgIconsEnabled = areSvgIconsEnabled();
    std::vector<TileGridItem> items;
    items.reserve(collected.size());
    for (const auto& manifest : collected) {
        bool recolor = false;
        const lv_image_dsc_t* image = nullptr;
        if (svgIconsEnabled) {
            image = packageIcon(manifest.id, &recolor);
            if (image == nullptr) {
                image = builtInIcon(manifest.id, &recolor);
            }
        }
        // The glyph is shown when the SVG icons are disabled or the image failed to load
        items.push_back({ manifest.id, manifest.name, LVGL_ICON_SHARED_DEPLOYED_CODE, Favourites::contains(favouriteIds, manifest.id), image, recolor });
    }
    ::std::ranges::sort(items, [](const TileGridItem& a, const TileGridItem& b) {
        if (a.highlighted != b.highlighted) {
            return a.highlighted;
        }
        const int nameOrder = strcasecmp(a.name.c_str(), b.name.c_str());
        if (nameOrder != 0) {
            return nameOrder < 0;
        }
        return a.id < b.id;
    });
    return items;
}

void toggleFavourite(TileGrid& grid, const char* appId, bool keepSelection) {
    auto* job = new (::std::nothrow) SaveFavouritesJob();
    if (job == nullptr) {
        LOG_E(TAG, "Failed to queue saving favourites");
        return;
    }
    if (!favourites.toggle(appId, job->ids)) {
        // The favourites are still being loaded, so toggling would overwrite the file with an incomplete list
        LOG_W(TAG, "Favourites aren't loaded yet");
        delete job;
        return;
    }
    if (file_ops_queue(saveFavourites, job, nullptr) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue saving favourites");
        delete job;
    }
    grid.requestRepopulate(keepSelection);
}

void onLongPressed(TileGrid& grid, const TileGridItem& item) {
    toggleFavourite(grid, item.id.c_str(), false);
}

void onKey(TileGrid& grid, const TileGridItem& item, uint32_t key) {
    if (key == 'f' || key == 'F') {
        toggleFavourite(grid, item.id.c_str(), true);
    }
}

} // namespace

const lv_image_dsc_t* defaultAppIcon(bool* recolor) {
    auto& state = packageIcons();
    mutex_lock(&state.mutex);
    const lv_image_dsc_t* image = toImageDsc(state.defaultIcon.image);
    *recolor = state.defaultIcon.recolor;
    mutex_unlock(&state.mutex);
    return image;
}

bool areSvgIconsEnabled() {
#ifdef ESP_PLATFORM
    return memory_external_total() > 0;
#else
    return true;
#endif
}

void releaseRetiredAppIcons() {
    auto& state = packageIcons();
    mutex_lock(&state.mutex);
    for (auto* image : state.retired) {
        lv_draw_buf_destroy(image);
    }
    state.retired.clear();
    mutex_unlock(&state.mutex);
}

bool areAppIconsCurrent() {
    if (!areSvgIconsEnabled()) return true;
    auto& state = packageIcons();
    mutex_lock(&state.mutex);
    const bool current = state.builtInLoaded && state.builtInForMono == lvgl_theme_is_mono();
    mutex_unlock(&state.mutex);
    return current;
}

void loadFavourites(void*) {
    favourites.load();
}

namespace {

struct PackageApps {
    std::string packageId;
    std::vector<std::string> appIds;
};

void collectPackageApps(const AppPackage* package, void* context) {
    auto* packages = static_cast<std::vector<PackageApps>*>(context);
    PackageApps entry { package->package.id, {} };
    for (size_t i = 0; i < package->app_id_count; i++) {
        entry.appIds.emplace_back(package->app_ids[i]);
    }
    packages->push_back(::std::move(entry));
}

PackageIcon rasterizeBuiltIn(const BuiltInIconAsset& asset, uint32_t size, bool preferMono) {
    bool mono = false;
    auto* image = lvgl_svg_rasterize_variants(
        reinterpret_cast<const char*>(asset.colour), asset.colourSize,
        reinterpret_cast<const char*>(asset.mono), asset.monoSize,
        preferMono, size, &mono
    );
    if (image == nullptr) {
        LOG_W(TAG, "Failed to rasterize the icon of %s", asset.id);
    }
    return { asset.id, {}, {}, {}, image, mono, preferMono };
}

// Replaces the built-in icons, which is needed on the first load and when the theme changed
void loadBuiltInIcons(PackageIcons& state, uint32_t size, bool preferMono) {
    std::vector<PackageIcon> icons;
    for (const auto& asset : BUILT_IN_ICONS) {
        auto icon = rasterizeBuiltIn(asset, size, preferMono);
        if (icon.image != nullptr) {
            icons.push_back(::std::move(icon));
        }
    }
    auto defaultIcon = rasterizeBuiltIn(DEFAULT_ICON, size, preferMono);

    mutex_lock(&state.mutex);
    for (const auto& icon : state.builtIn) {
        state.retired.push_back(icon.image);
    }
    if (state.defaultIcon.image != nullptr) {
        state.retired.push_back(state.defaultIcon.image);
    }
    state.builtIn = ::std::move(icons);
    state.defaultIcon = ::std::move(defaultIcon);
    state.builtInLoaded = true;
    state.builtInForMono = preferMono;
    mutex_unlock(&state.mutex);
}

} // namespace

void loadPackageIcons(void*) {
    if (!areSvgIconsEnabled()) return;

    auto& state = packageIcons();

    const auto size = static_cast<uint32_t>(lvgl_get_shared_icon_large_font_height());
    const bool preferMono = lvgl_theme_is_mono();
    if (!state.builtInLoaded || state.builtInForMono != preferMono) {
        loadBuiltInIcons(state, size, preferMono);
    }

    std::vector<PackageApps> packages;
    app_manager_for_each_package(collectPackageApps, &packages);

    // Only this task changes the icons, so reading them here needs no lock
    std::vector<PackageIcon> loaded;
    std::vector<bool> reused(state.icons.size(), false);
    for (auto& package : packages) {
        // The apps of a package share its directory, which depends on where the package was registered
        if (package.appIds.empty()) continue;
        char install_path[256];
        if (app_paths_get_install_directory(package.appIds.front().c_str(), install_path, sizeof(install_path)) != ERROR_NONE) continue;

        // Either variant can be missing: the other one is used instead
        const std::string colour_path = std::string(install_path) + "/" + PACKAGE_ICON_FILE;
        const std::string mono_path = std::string(install_path) + "/" + PACKAGE_ICON_MONO_FILE;
        struct stat colour_stat;
        struct stat mono_stat;
        const bool has_colour = stat(colour_path.c_str(), &colour_stat) == 0;
        const bool has_mono = stat(mono_path.c_str(), &mono_stat) == 0;
        if (!has_colour && !has_mono) continue;
        const IconFileStamp colour_file { has_colour, has_colour ? colour_stat.st_mtime : 0 };
        const IconFileStamp mono_file { has_mono, has_mono ? mono_stat.st_mtime : 0 };

        // An unchanged icon keeps its image
        bool found = false;
        for (size_t i = 0; i < state.icons.size(); i++) {
            const auto& existing = state.icons[i];
            if (existing.packageId == package.packageId && existing.colourFile == colour_file && existing.monoFile == mono_file && existing.forMono == preferMono && existing.appIds == package.appIds) {
                loaded.push_back(existing);
                reused[i] = true;
                found = true;
                break;
            }
        }
        if (found) continue;

        // One rasterized image per package, shared by its apps
        bool mono = false;
        lv_draw_buf_t* image = lvgl_svg_rasterize_file_variants(
            has_colour ? colour_path.c_str() : nullptr,
            has_mono ? mono_path.c_str() : nullptr,
            preferMono, size, &mono
        );
        if (image == nullptr) {
            LOG_W(TAG, "Failed to load the icon of %s", package.packageId.c_str());
            continue;
        }
        loaded.push_back({ package.packageId, ::std::move(package.appIds), colour_file, mono_file, image, mono, preferMono });
    }

    mutex_lock(&state.mutex);
    for (size_t i = 0; i < state.icons.size(); i++) {
        if (!reused[i]) {
            state.retired.push_back(state.icons[i].image);
        }
    }
    state.icons = ::std::move(loaded);
    mutex_unlock(&state.mutex);
}

const LauncherMode APPS_MODE = {
    .buttonIcon = LVGL_ICON_SHARED_APPS,
    .iconColor = TileGrid::IconColor::Primary,
    .collect = collectItems,
    .onLongPressed = onLongPressed,
    .onKey = onKey
};

}
