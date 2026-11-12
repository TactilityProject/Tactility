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

std::string app_hub_catalog_get_url() {
    return std::format("{}/{}/apps.json", BASE_URL, get_version_without_postfix());
}

std::string app_hub_catalog_get_download_url(const std::string& file) {
    return std::format("{}/{}/{}", BASE_URL, get_version_without_postfix(), file);
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
    // Optional: absent in apps.json files from before these fields existed
    if (cJSON_HasObjectItem(object, "requiresDeviceId") && !read_string_array(object, "requiresDeviceId", entry.requires_device_id)) {
        return false;
    }
    if (cJSON_HasObjectItem(object, "requiresRam") && !read_int32(object, "requiresRam", entry.requires_ram)) {
        return false;
    }
    return read_string(object, "appId", entry.id) &&
        read_string(object, "appVersionName", entry.version_name) &&
        read_int32(object, "appVersionCode", entry.version_code) &&
        read_string(object, "appName", entry.name) &&
        read_string(object, "appDescription", entry.description) &&
        read_string(object, "targetSdk", entry.target_sdk) &&
        read_string(object, "file", entry.file) &&
        read_string_array(object, "targetPlatforms", entry.target_platforms);
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
constexpr char LIST_SEPARATOR = ',';
constexpr size_t FIELD_COUNT = 10;
// Bounds the stack buffer of a reader. A longer entry makes the conversion fail.
constexpr size_t LINE_MAX_SIZE = 1024;

/** Control characters, such as the field separator, can't be in a catalog line */
void append_text(std::string& line, const std::string& text) {
    for (const char character : text) {
        line += (static_cast<unsigned char>(character) < ' ') ? ' ' : character;
    }
    line += FIELD_SEPARATOR;
}

bool append_list(std::string& line, const std::vector<std::string>& items) {
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].find(LIST_SEPARATOR) != std::string::npos) {
            return false;
        }
        if (i > 0) {
            line += LIST_SEPARATOR;
        }
        append_text(line, items[i]);
        line.pop_back();
    }
    line += FIELD_SEPARATOR;
    return true;
}

bool format_entry(const AppHubCatalogEntry& entry, std::string& line) {
    line.clear();
    append_text(line, entry.id);
    append_text(line, entry.version_name);
    line += std::to_string(entry.version_code) + FIELD_SEPARATOR;
    append_text(line, entry.name);
    append_text(line, entry.description);
    append_text(line, entry.target_sdk);
    if (!append_list(line, entry.target_platforms) || !append_list(line, entry.requires_device_id)) {
        return false;
    }
    line += std::to_string(entry.requires_ram) + FIELD_SEPARATOR;
    append_text(line, entry.file);
    line.back() = '\n';
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

std::vector<std::string> split_list(const std::string& text) {
    return text.empty() ? std::vector<std::string> {} : split(text, LIST_SEPARATOR);
}

bool parse_line(std::string_view line, AppHubCatalogEntry& entry) {
    const auto fields = split(line, FIELD_SEPARATOR);
    if (fields.size() != FIELD_COUNT) {
        return false;
    }
    char* end = nullptr;
    const long version_code = strtol(fields[2].c_str(), &end, 10);
    if (fields[2].empty() || *end != '\0') {
        return false;
    }
    const long requires_ram = strtol(fields[8].c_str(), &end, 10);
    if (fields[8].empty() || *end != '\0') {
        return false;
    }
    entry.id = fields[0];
    entry.version_name = fields[1];
    entry.version_code = static_cast<int32_t>(version_code);
    entry.name = fields[3];
    entry.description = fields[4];
    entry.target_sdk = fields[5];
    entry.target_platforms = split_list(fields[6]);
    entry.requires_device_id = split_list(fields[7]);
    entry.requires_ram = static_cast<int32_t>(requires_ram);
    entry.file = fields[9];
    return true;
}

} // namespace

bool app_hub_catalog_convert(const char* json_path, const char* catalog_path) {
    auto data = read_file(json_path);
    if (data == nullptr) {
        LOG_E(TAG, "Failed to read %s", json_path);
        return false;
    }

    cJSON* json = cJSON_Parse(data.get());
    data = nullptr;
    if (json == nullptr) {
        LOG_E(TAG, "Failed to parse %s", json_path);
        return false;
    }
    const cJSON* apps = cJSON_GetObjectItemCaseSensitive(json, "apps");
    if (!cJSON_IsArray(apps)) {
        LOG_E(TAG, "apps is not an array");
        cJSON_Delete(json);
        return false;
    }

    FILE* file = fopen(catalog_path, "wb");
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
    success = (fclose(file) == 0) && success;
    if (!success) {
        remove(catalog_path);
    }
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

bool app_hub_catalog_is_compatible(const AppHubCatalogEntry& entry) {
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
        LOG_W(TAG, "%s: requiresDeviceId too long", entry.id.c_str());
        return false;
    }
    strcpy(package.requires_device_id, device_ids.c_str());
    if (entry.target_sdk.size() >= sizeof(package.target_sdk)) {
        LOG_W(TAG, "%s: targetSdk too long", entry.id.c_str());
        return false;
    }
    strcpy(package.target_sdk, entry.target_sdk.c_str());
    if (entry.requires_ram < 0 || entry.requires_ram > UINT8_MAX) {
        LOG_W(TAG, "%s: invalid requiresRam %d", entry.id.c_str(), static_cast<int>(entry.requires_ram));
        return false;
    }
    package.requires_ram = static_cast<uint8_t>(entry.requires_ram);
    return app_package_manifest_is_compatible(&package);
}
