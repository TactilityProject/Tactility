// SPDX-License-Identifier: Apache-2.0
#include <app_hub/app_hub_catalog.h>
#include <app_hub/module.h>

#include <app/package_manifest.h>

#include <tactility/log.h>
#include <tactility/paths.h>

#include <cJSON.h>

#include <new>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <cstdlib>
#include <memory>
#include <string_view>

constexpr auto* TAG = "app_hub_catalog";
constexpr auto* BASE_URL = "https://cdn.tactilityproject.org/apps";

static std::string get_version_without_postfix() {
    std::string version(TT_VERSION);
    auto index = version.find_first_of('-');
    return (index == std::string::npos) ? version : version.substr(0, index);
}

static const char* certificate_pem = "";

void app_hub_configure_certificate(const char* pem) {
    certificate_pem = pem;
}

const char* app_hub_catalog_get_certificate() {
    return certificate_pem;
}

const char* app_hub_catalog_get_official_url() {
    return BASE_URL;
}

std::string app_hub_catalog_get_page_url(const std::string& base_url, int32_t page) {
    return std::format("{}/{}/list-{}-{}.json", base_url, get_version_without_postfix(), app_hub_catalog_get_platform(), page);
}

std::string app_hub_catalog_get_download_url(const std::string& base_url, const std::string& file) {
    return std::format("{}/{}/{}", base_url, get_version_without_postfix(), file);
}

std::string app_hub_catalog_get_details_file(const std::string& id) {
    return id + ".json";
}

std::string app_hub_catalog_get_icon_file(const std::string& id, int32_t version_code, bool mono) {
    return id + "-" + std::to_string(version_code) + (mono ? "-icon-mono.svg" : "-icon.svg");
}

static bool read_string(const cJSON* object, const char* key, std::string& value) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item)) {
        LOG_E(TAG, "%s is not a string", key);
        return false;
    }
    value = item->valuestring;
    return true;
}

static bool read_int32(const cJSON* object, const char* key, int32_t& value) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || item->valuedouble != static_cast<double>(static_cast<int32_t>(item->valuedouble))) {
        LOG_E(TAG, "%s is not an integer", key);
        return false;
    }
    value = static_cast<int32_t>(item->valuedouble);
    return true;
}

static bool read_string_array(const cJSON* object, const char* key, std::vector<std::string>& values) {
    const cJSON* array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsArray(array)) {
        LOG_E(TAG, "%s is not an array", key);
        return false;
    }
    values.clear();
    const cJSON* item;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item)) {
            LOG_E(TAG, "%s has an item that is not a string", key);
            return false;
        }
        values.emplace_back(item->valuestring);
    }
    return true;
}

static bool parse_entry(const cJSON* object, AppHubCatalogEntry& entry) {
    // Optional: only present for apps without an icon
    const cJSON* icon = cJSON_GetObjectItemCaseSensitive(object, "icon");
    if (icon != nullptr) {
        if (!cJSON_IsBool(icon)) {
            LOG_E(TAG, "icon is not a boolean");
            return false;
        }
        entry.has_icon = cJSON_IsTrue(icon);
    }
    return read_string(object, "id", entry.id) &&
        read_int32(object, "version", entry.version_code) &&
        read_string(object, "name", entry.name) &&
        read_string(object, "description", entry.short_description);
}

static bool parse_details(const cJSON* object, AppHubCatalogDetails& details) {
    // Optional: absent when the app has no such requirement
    if (cJSON_HasObjectItem(object, "requiresDeviceId") && !read_string_array(object, "requiresDeviceId", details.requires_device_id)) {
        return false;
    }
    if (cJSON_HasObjectItem(object, "requiresRam") && !read_int32(object, "requiresRam", details.requires_ram)) {
        return false;
    }
    return read_string(object, "appDescription", details.description) &&
        read_string(object, "appVersionName", details.version_name) &&
        read_string(object, "targetSdk", details.target_sdk) &&
        read_string(object, "file", details.file) &&
        read_string_array(object, "targetPlatforms", details.target_platforms);
}

static std::unique_ptr<char[]> read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return nullptr;
    }
    std::unique_ptr<char[]> data;
    if (fseek(file, 0, SEEK_END) == 0) {
        const long size = ftell(file);
        if (size >= 0 && fseek(file, 0, SEEK_SET) == 0) {
            // Exceptions are disabled, so a failed allocation must not abort
            data.reset(new (std::nothrow) char[size + 1]);
            if (data != nullptr && fread(data.get(), 1, size, file) == static_cast<size_t>(size)) {
                data[size] = '\0';
            } else {
                data = nullptr;
            }
        }
    }
    fclose(file);
    return data;
}

namespace {

constexpr char FIELD_SEPARATOR = '\t';
// Catalogs from before the repositories have no repository field
constexpr size_t FIELD_COUNT_WITHOUT_REPOSITORY = 5;
constexpr size_t FIELD_COUNT = 6;
// Bounds the stack buffer of a reader. A longer entry makes the conversion fail.
constexpr size_t LINE_MAX_SIZE = 512;

/** Control characters, such as the field separator, can't be in a catalog line */
void append_text(std::string& line, const std::string& text) {
    for (const char character : text) {
        line += (static_cast<unsigned char>(character) < ' ') ? ' ' : character;
    }
    line += FIELD_SEPARATOR;
}

bool format_entry(const AppHubCatalogEntry& entry, std::string& line) {
    line.clear();
    append_text(line, entry.id);
    line += std::to_string(entry.version_code) + FIELD_SEPARATOR;
    append_text(line, entry.name);
    append_text(line, entry.short_description);
    line += entry.has_icon ? '1' : '0';
    line += FIELD_SEPARATOR;
    line += std::to_string(entry.repository);
    line += '\n';
    return line.size() < LINE_MAX_SIZE;
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            return parts;
        }
        parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
}

bool parse_line(std::string_view line, AppHubCatalogEntry& entry) {
    const auto fields = split(line, FIELD_SEPARATOR);
    if (fields.size() != FIELD_COUNT && fields.size() != FIELD_COUNT_WITHOUT_REPOSITORY) {
        return false;
    }
    char* end = nullptr;
    const long version_code = strtol(fields[1].c_str(), &end, 10);
    if (fields[1].empty() || *end != '\0') {
        return false;
    }
    entry.id = fields[0];
    entry.version_code = static_cast<int32_t>(version_code);
    entry.name = fields[2];
    entry.short_description = fields[3];
    entry.has_icon = fields[4] != "0";
    entry.repository = 0;
    if (fields.size() == FIELD_COUNT) {
        const long repository = strtol(fields[5].c_str(), &end, 10);
        if (fields[5].empty() || *end != '\0' || repository < 0) {
            return false;
        }
        entry.repository = static_cast<int32_t>(repository);
    }
    return true;
}

cJSON* parse_file(const char* path) {
    auto data = read_file(path);
    if (data == nullptr) {
        LOG_E(TAG, "Failed to read %s", path);
        return nullptr;
    }
    cJSON* json = cJSON_Parse(data.get());
    if (json == nullptr) {
        LOG_E(TAG, "Failed to parse %s", path);
    }
    return json;
}

} // namespace

bool app_hub_catalog_append_page(const char* json_path, const char* catalog_path, int32_t& page_count, int32_t repository, AppHubCatalogAccept accept, void* context) {
    cJSON* json = parse_file(json_path);
    if (json == nullptr) {
        return false;
    }
    const cJSON* apps = cJSON_GetObjectItemCaseSensitive(json, "apps");
    if (!cJSON_IsArray(apps)) {
        LOG_E(TAG, "apps is not an array");
        cJSON_Delete(json);
        return false;
    }
    if (!read_int32(json, "pageCount", page_count) || page_count < 1) {
        LOG_E(TAG, "pageCount is invalid");
        cJSON_Delete(json);
        return false;
    }

    FILE* file = fopen(catalog_path, "ab");
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open %s", catalog_path);
        cJSON_Delete(json);
        return false;
    }
    bool success = true;
    std::string line;
    const cJSON* item;
    cJSON_ArrayForEach(item, apps) {
        AppHubCatalogEntry entry;
        if (!parse_entry(item, entry)) {
            LOG_E(TAG, "Failed to read entry");
            success = false;
            break;
        }
        entry.repository = repository;
        if (accept != nullptr && !accept(entry, context)) {
            continue;
        }
        if (!format_entry(entry, line)) {
            LOG_E(TAG, "%s can't be stored", entry.id.c_str());
            success = false;
            break;
        }
        if (fwrite(line.data(), 1, line.size(), file) != line.size()) {
            LOG_E(TAG, "Failed to write %s", catalog_path);
            success = false;
            break;
        }
    }
    cJSON_Delete(json);
    return (fclose(file) == 0) && success;
}

bool app_hub_catalog_read_details(const char* json_path, AppHubCatalogDetails& details) {
    cJSON* json = parse_file(json_path);
    if (json == nullptr) {
        return false;
    }
    const bool success = parse_details(json, details);
    cJSON_Delete(json);
    return success;
}

bool app_hub_catalog_for_each(const char* catalog_path, AppHubCatalogVisitor visitor, void* context) {
    FILE* file = fopen(catalog_path, "rb");
    if (file == nullptr) {
        return false;
    }
    bool success = true;
    char line[LINE_MAX_SIZE];
    while (fgets(line, sizeof(line), file) != nullptr) {
        const size_t length = strlen(line);
        AppHubCatalogEntry entry;
        if (length == 0 || line[length - 1] != '\n' || !parse_line(std::string_view(line, length - 1), entry)) {
            LOG_E(TAG, "Malformed catalog %s", catalog_path);
            success = false;
            break;
        }
        if (!visitor(entry, context)) {
            break;
        }
    }
    fclose(file);
    return success;
}

bool app_hub_catalog_find(const char* catalog_path, const char* id, AppHubCatalogEntry& entry) {
    struct Search {
        const char* id;
        AppHubCatalogEntry& entry;
        bool found;
    } search { id, entry, false };
    app_hub_catalog_for_each(catalog_path, [](const AppHubCatalogEntry& candidate, void* context) {
        auto* search = static_cast<Search*>(context);
        if (candidate.id != search->id) {
            return true;
        }
        search->entry = candidate;
        search->found = true;
        return false;
    }, &search);
    return search.found;
}

const char* app_hub_catalog_get_platform() {
#ifdef ESP_PLATFORM
    return CONFIG_IDF_TARGET;
#else
    // Same name as the folder of the app binary that the POSIX app loader looks for
    return TACTILITY_POSIX_PLATFORM;
#endif
}

bool app_hub_catalog_is_compatible(const AppHubCatalogDetails& entry) {
    // An empty list means that the app supports every platform
    if (!entry.target_platforms.empty() &&
        std::ranges::find(entry.target_platforms, std::string_view(app_hub_catalog_get_platform())) == entry.target_platforms.end()) {
        return false;
    }

    // Same device and RAM rules as for installed packages
    PackageManifest package {};
    std::string device_ids;
    for (const auto& device_id : entry.requires_device_id) {
        if (!device_ids.empty()) {
            device_ids += ',';
        }
        device_ids += device_id;
    }
    if (device_ids.size() >= sizeof(package.requires_device_id)) {
        LOG_W(TAG, "requiresDeviceId too long");
        return false;
    }
    strcpy(package.requires_device_id, device_ids.c_str());
    if (entry.target_sdk.size() >= sizeof(package.target_sdk)) {
        LOG_W(TAG, "targetSdk too long");
        return false;
    }
    strcpy(package.target_sdk, entry.target_sdk.c_str());
    if (entry.requires_ram < 0 || entry.requires_ram > UINT8_MAX) {
        LOG_W(TAG, "Invalid requiresRam %d", static_cast<int>(entry.requires_ram));
        return false;
    }
    package.requires_ram = static_cast<uint8_t>(entry.requires_ram);
    return app_package_manifest_is_compatible(&package);
}
