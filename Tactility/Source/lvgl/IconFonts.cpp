#include <Tactility/lvgl/IconFonts.h>

#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>

#include <binfont/binfont.h>
#include <binfont/generator.h>
#include <lvgl/binfont.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/names.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace tt::lvgl {

constexpr auto* TAG = "IconFonts";
constexpr uint8_t ICON_FONT_BPP = 2;
constexpr auto* CACHE_FILE_PREFIX = "material_symbols_";

struct IconFontDefinition {
    LvglIconFont id;
    const char* name;
    uint16_t size;
    const char* const* iconNames;
    size_t iconNameCount;
};

static std::string getSystemFontPath(const char* fileName) {
    return std::format("{}/fonts/{}", file::MOUNT_POINT_SYSTEM, fileName);
}

static std::string getCacheDirectory() {
    char data_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(data_path, sizeof(data_path)) != ERROR_NONE) {
        return "";
    }
    auto directory = std::format("{}/cache/fonts", data_path);
    if (!file::findOrCreateDirectory(directory, 0777)) {
        LOG_W(TAG, "Failed to create %s", directory.c_str());
        return "";
    }
    return directory;
}

static std::string getCacheFileName(const IconFontDefinition& definition) {
    return std::format("{}{}_{}_{:08x}.bin", CACHE_FILE_PREFIX, definition.name, definition.size, LVGL_ICON_FONT_VERSION);
}

static void deleteStaleCacheFiles(const std::string& directory, const std::vector<std::string>& currentFileNames) {
    std::vector<std::string> stale;
    file::listDirectory(directory, [&](const dirent& entry) {
        const std::string name = entry.d_name;
        if (name.starts_with(CACHE_FILE_PREFIX) && std::find(currentFileNames.begin(), currentFileNames.end(), name) == currentFileNames.end()) {
            stale.push_back(name);
        }
    });
    for (const auto& name : stale) {
        LOG_I(TAG, "Deleting stale %s", name.c_str());
        file::deleteFile(file::getChildPath(directory, name));
    }
}

/** Parses a .codepoints file: one "<name> <hex codepoint>" pair per line */
static std::unordered_map<std::string, uint32_t> loadCodepoints(const std::string& path) {
    std::unordered_map<std::string, uint32_t> codepoints;
    file::readLines(path, true, [&codepoints](const char* line) {
        const char* separator = strchr(line, ' ');
        if (separator != nullptr) {
            codepoints[std::string(line, separator - line)] = static_cast<uint32_t>(strtoul(separator + 1, nullptr, 16));
        }
    });
    return codepoints;
}

static BinFont* generateFont(const IconFontDefinition& definition, const std::unordered_map<std::string, uint32_t>& codepointMap, const std::string& cachePath) {
    std::vector<uint32_t> codepoints;
    codepoints.reserve(definition.iconNameCount);
    for (size_t i = 0; i < definition.iconNameCount; i++) {
        const auto entry = codepointMap.find(definition.iconNames[i]);
        if (entry != codepointMap.end()) {
            codepoints.push_back(entry->second);
        } else {
            LOG_W(TAG, "No codepoint for icon %s", definition.iconNames[i]);
        }
    }

    const auto ttf_path = getSystemFontPath("MaterialSymbolsRounded.ttf");
    const BinFontGeneratorConfig config = {
        .ttf_path = ttf_path.c_str(),
        .size = definition.size,
        .bpp = ICON_FONT_BPP,
        .codepoints = codepoints.data(),
        .codepoint_count = codepoints.size(),
    };

    const auto start_time = get_millis();
    uint8_t* data = nullptr;
    size_t size = 0;
    const error_t error = binfont_generate(&config, &data, &size);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to generate %s font from %s (%s)", definition.name, ttf_path.c_str(), error_to_string(error));
        return nullptr;
    }
    LOG_I(TAG, "Generated %s font (%zu bytes) in %zu ms", definition.name, size, get_millis() - start_time);

    if (!cachePath.empty()) {
        FILE* file = fopen(cachePath.c_str(), "wb");
        const bool written = file != nullptr && fwrite(data, 1, size, file) == size;
        if (file != nullptr && fclose(file) != 0) {
            LOG_W(TAG, "Failed to close %s", cachePath.c_str());
        }
        if (!written) {
            LOG_W(TAG, "Failed to write %s", cachePath.c_str());
            file::deleteFile(cachePath);
        }
    }

    BinFont* font = nullptr;
    if (binfont_open_memory(data, size, true, &font) != ERROR_NONE) {
        LOG_E(TAG, "Failed to open generated %s font", definition.name);
        return nullptr;
    }
    return font;
}

void initIconFonts() {
    const IconFontDefinition definitions[] = {
        { LVGL_ICON_FONT_STATUSBAR, "statusbar", TT_LVGL_STATUSBAR_FONT_ICON_SIZE, lvgl_icon_statusbar_names, lvgl_icon_statusbar_name_count },
        { LVGL_ICON_FONT_LAUNCHER, "launcher", TT_LVGL_LAUNCHER_FONT_ICON_SIZE, lvgl_icon_launcher_names, lvgl_icon_launcher_name_count },
        { LVGL_ICON_FONT_SHARED, "shared", TT_LVGL_SHARED_FONT_ICON_SIZE, lvgl_icon_shared_names, lvgl_icon_shared_name_count },
        { LVGL_ICON_FONT_SHARED_2X, "shared2x", TT_LVGL_SHARED_FONT_ICON_2X_SIZE, lvgl_icon_shared_names, lvgl_icon_shared_name_count },
    };

    const auto cache_directory = getCacheDirectory();
    if (cache_directory.empty()) {
        LOG_W(TAG, "No data path available, icon fonts won't be cached");
    } else {
        std::vector<std::string> file_names;
        for (const auto& definition : definitions) {
            file_names.push_back(getCacheFileName(definition));
        }
        deleteStaleCacheFiles(cache_directory, file_names);
    }

    std::unordered_map<std::string, uint32_t> codepoint_map;
    for (const auto& definition : definitions) {
        const auto cache_path = cache_directory.empty() ? std::string() : file::getChildPath(cache_directory, getCacheFileName(definition));

        BinFont* font = nullptr;
        if (!cache_path.empty() && file::isFile(cache_path)) {
            if (binfont_open_file(cache_path.c_str(), &font) != ERROR_NONE) {
                LOG_W(TAG, "Failed to open %s, regenerating", cache_path.c_str());
                font = nullptr;
            }
        }

        if (font == nullptr) {
            if (codepoint_map.empty()) {
                codepoint_map = loadCodepoints(getSystemFontPath("MaterialSymbolsRounded.codepoints"));
            }
            font = generateFont(definition, codepoint_map, cache_path);
            if (font == nullptr) {
                continue;
            }
        }

        lv_font_t* lv_font = lvgl_binfont_create(font);
        if (lv_font == nullptr) {
            LOG_E(TAG, "Failed to create LVGL font for %s", definition.name);
            binfont_close(font);
            continue;
        }
        lvgl_set_icon_font(definition.id, lv_font);
    }
}

}
