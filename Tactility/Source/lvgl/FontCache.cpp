#include <Tactility/lvgl/FontCache.h>

#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>

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
#include <sys/stat.h>
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

static bool writeCacheFile(const std::string& path, const uint8_t* data, size_t size) {
    FILE* file = fopen(path.c_str(), "wb");
    bool written = file != nullptr && fwrite(data, 1, size, file) == size;
    if (file != nullptr && fclose(file) != 0) {
        LOG_W(TAG, "Failed to close %s", path.c_str());
        written = false;
    }
    if (!written) {
        LOG_W(TAG, "Failed to write %s", path.c_str());
        file::deleteFile(path);
    }
    return written;
}

/** Rasterizes a font, see binfont_generate() */
static bool generateFont(const std::string& fileName, const std::string& ttfPath, uint16_t size, uint8_t bpp, const std::function<std::vector<uint32_t>()>& getCodepoints, uint8_t*& data, size_t& dataSize) {
    const auto codepoints = getCodepoints();
    const BinFontGeneratorConfig config = {
        .ttf_path = ttfPath.c_str(),
        .size = size,
        .bpp = bpp,
        .codepoints = codepoints.empty() ? nullptr : codepoints.data(),
        .codepoint_count = codepoints.size(),
    };

    const auto start_time = get_millis();
    const error_t error = binfont_generate(&config, &data, &dataSize);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to generate %s from %s (%s)", fileName.c_str(), ttfPath.c_str(), error_to_string(error));
        return false;
    }
    LOG_I(TAG, "Generated %s (%zu bytes) in %zu ms", fileName.c_str(), dataSize, get_millis() - start_time);
    return true;
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

    uint8_t* data = nullptr;
    size_t data_size = 0;
    if (!generateFont(fileName, ttfPath, size, bpp, getCodepoints, data, data_size)) {
        return nullptr;
    }

    if (!cache_path.empty()) {
        writeCacheFile(cache_path, data, data_size);
    }

    if (binfont_open_memory(data, data_size, true, &font) != ERROR_NONE) {
        LOG_E(TAG, "Failed to open generated %s", fileName.c_str());
        return nullptr;
    }
    return font;
}

bool ensureCachedFont(const std::string& fileName, const std::string& ttfPath, uint16_t size, uint8_t bpp, const std::function<std::vector<uint32_t>()>& getCodepoints) {
    const auto& directory = getCacheDirectory();
    const auto cache_path = directory.empty() ? std::string() : file::getChildPath(directory, fileName);
    if (!cache_path.empty() && file::isFile(cache_path)) {
        return true;
    }

    uint8_t* data = nullptr;
    size_t data_size = 0;
    if (!generateFont(fileName, ttfPath, size, bpp, getCodepoints, data, data_size)) {
        return false;
    }
    // Without a cache, the font is only generated to verify that it can be
    const bool written = cache_path.empty() || writeCacheFile(cache_path, data, data_size);
    memory_free(data);
    return written;
}

uint32_t getFontFileVersion(const std::string& path) {
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) != 0) {
        return 0;
    }
    // FNV-1a over the path, size and modification time
    uint32_t hash = 2166136261u;
    auto add = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; i++) {
            hash = (hash ^ bytes[i]) * 16777619u;
        }
    };
    add(path.data(), path.size());
    const auto file_size = static_cast<uint64_t>(file_stat.st_size);
    const auto modified = static_cast<int64_t>(file_stat.st_mtime);
    add(&file_size, sizeof(file_size));
    add(&modified, sizeof(modified));
    return hash != 0 ? hash : 1;
}

}
