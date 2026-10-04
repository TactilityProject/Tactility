#include <Tactility/lvgl/FontCache.h>

#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>
#include <Tactility/lvgl/FontVersions.h>

#include <binfont/binfont.h>
#include <binfont/generator.h>
#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <algorithm>
#include <cstdio>
#include <format>

namespace tt::lvgl {

constexpr auto* TAG = "FontCache";

/** @return the font cache directory, or an empty string when there is no data path */
static const std::string& getCacheDirectory() {
    static const std::string directory = [] {
        char data_path[FILE_MAX_PATH_STRING_LENGTH];
        if (paths_get_data_path(data_path, sizeof(data_path)) != ERROR_NONE) {
            LOG_W(TAG, "No data path available, generated fonts won't be cached");
            return std::string();
        }
        auto path = std::format("{}/cache/fonts", data_path);
        if (!file::findOrCreateDirectory(path, 0777)) {
            LOG_W(TAG, "Failed to create %s", path.c_str());
            return std::string();
        }
        return path;
    }();
    return directory;
}

std::string getSystemFontPath(const char* fileName) {
    return std::format("{}/fonts/{}", file::MOUNT_POINT_SYSTEM, fileName);
}

uint8_t getGeneratedFontBpp() {
    Device* display = nullptr;
    if (device_get_first_by_type(&DISPLAY_TYPE, &display) == ERROR_NONE) {
        const bool monochrome = display_get_color_format(display) == DISPLAY_COLOR_FORMAT_MONOCHROME;
        device_put(display);
        if (monochrome) {
            return 1;
        }
    }
    return memory_external_total() > 0 ? 4 : 2;
}

std::string getCachedFontFileName(const char* prefix, const char* name, uint16_t size, uint8_t bpp, uint32_t version) {
    return std::format("{}{}_{}_{}bpp_{:08x}.bin", prefix, name, size, bpp, version);
}

void deleteStaleCachedFonts(const char* prefix, const std::vector<std::string>& currentFileNames) {
    const auto& directory = getCacheDirectory();
    if (directory.empty()) {
        return;
    }
    std::vector<std::string> stale;
    file::listDirectory(directory, [&](const dirent& entry) {
        const std::string name = entry.d_name;
        if (name.starts_with(prefix) && std::find(currentFileNames.begin(), currentFileNames.end(), name) == currentFileNames.end()) {
            stale.push_back(name);
        }
    });
    for (const auto& name : stale) {
        LOG_I(TAG, "Deleting stale %s", name.c_str());
        file::deleteFile(file::getChildPath(directory, name));
    }
}

static void writeCacheFile(const std::string& path, const uint8_t* data, size_t size) {
    FILE* file = fopen(path.c_str(), "wb");
    const bool written = file != nullptr && fwrite(data, 1, size, file) == size;
    if (file != nullptr && fclose(file) != 0) {
        LOG_W(TAG, "Failed to close %s", path.c_str());
    }
    if (!written) {
        LOG_W(TAG, "Failed to write %s", path.c_str());
        file::deleteFile(path);
    }
}

BinFont* loadOrGenerateFont(const std::string& fileName, const std::string& ttfPath, uint16_t size, uint8_t bpp, const std::function<std::vector<uint32_t>()>& getCodepoints) {
    const auto& directory = getCacheDirectory();
    const auto cache_path = directory.empty() ? std::string() : file::getChildPath(directory, fileName);

    BinFont* font = nullptr;
    if (!cache_path.empty() && file::isFile(cache_path)) {
        if (binfont_open_file(cache_path.c_str(), &font) == ERROR_NONE) {
            return font;
        }
        LOG_W(TAG, "Failed to open %s, regenerating", cache_path.c_str());
    }

    const auto codepoints = getCodepoints();
    const BinFontGeneratorConfig config = {
        .ttf_path = ttfPath.c_str(),
        .size = size,
        .bpp = bpp,
        .codepoints = codepoints.empty() ? nullptr : codepoints.data(),
        .codepoint_count = codepoints.size(),
    };

    const auto start_time = get_millis();
    uint8_t* data = nullptr;
    size_t data_size = 0;
    const error_t error = binfont_generate(&config, &data, &data_size);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to generate %s from %s (%s)", fileName.c_str(), ttfPath.c_str(), error_to_string(error));
        return nullptr;
    }
    LOG_I(TAG, "Generated %s (%zu bytes) in %zu ms", fileName.c_str(), data_size, get_millis() - start_time);

    if (!cache_path.empty()) {
        writeCacheFile(cache_path, data, data_size);
    }

    if (binfont_open_memory(data, data_size, true, &font) != ERROR_NONE) {
        LOG_E(TAG, "Failed to open generated %s", fileName.c_str());
        return nullptr;
    }
    return font;
}

BinFont* loadMonoFont(uint16_t size) {
    constexpr auto* prefix = "adwaita_mono_";
    const uint8_t bpp = getGeneratedFontBpp();
    const auto file_name = getCachedFontFileName(prefix, "mono", size, bpp, TT_MONO_FONT_VERSION);
    deleteStaleCachedFonts(prefix, { file_name });
    // The TTF is a subset with only the supported characters, so all of its codepoints are used
    return loadOrGenerateFont(file_name, getSystemFontPath("AdwaitaMono.ttf"), size, bpp, [] { return std::vector<uint32_t>(); });
}

}
