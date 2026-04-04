#include <Tactility/bluetooth/BluetoothPairedDevice.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Logger.h>

#include <dirent.h>
#include <format>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstdio>

namespace tt::bluetooth::settings {

static const auto LOGGER = Logger("BluetoothPairedDevice");

// Use the same directory as the old service for backward compatibility.
constexpr auto* DATA_DIR               = "/data/service/bluetooth";
constexpr auto* DEVICE_SETTINGS_FORMAT = "{}/{}.device.properties";
constexpr auto* KEY_NAME        = "name";
constexpr auto* KEY_ADDR        = "addr";
constexpr auto* KEY_AUTO_CONNECT = "autoConnect";
constexpr auto* KEY_PROFILE_ID  = "profileId";

std::string addrToHex(const std::array<uint8_t, 6>& addr) {
    std::stringstream stream;
    stream << std::hex;
    for (int i = 0; i < 6; ++i) {
        stream << std::setw(2) << std::setfill('0') << static_cast<int>(addr[i]);
    }
    return stream.str();
}

static bool hexToAddr(const std::string& hex, std::array<uint8_t, 6>& addr) {
    if (hex.size() != 12) {
        LOGGER.error("hexToAddr() length mismatch: expected 12, got {}", hex.size());
        return false;
    }
    char buf[3] = { 0 };
    for (int i = 0; i < 6; ++i) {
        buf[0] = hex[i * 2];
        buf[1] = hex[i * 2 + 1];
        char* endptr = nullptr;
        addr[i] = static_cast<uint8_t>(strtoul(buf, &endptr, 16));
        if (endptr != buf + 2) {
            LOGGER.error("hexToAddr() invalid hex at byte {}: '{}{}'", i, buf[0], buf[1]);
            return false;
        }
    }
    return true;
}

static std::string getFilePath(const std::string& addr_hex) {
    return std::format(DEVICE_SETTINGS_FORMAT, DATA_DIR, addr_hex);
}

bool hasFileForDevice(const std::string& addr_hex) {
    return file::isFile(getFilePath(addr_hex));
}

bool load(const std::string& addr_hex, PairedDevice& device) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(getFilePath(addr_hex), map)) return false;
    if (!map.contains(KEY_ADDR)) return false;
    if (!hexToAddr(map[KEY_ADDR], device.addr)) return false;

    device.name = map.contains(KEY_NAME) ? map[KEY_NAME] : "";

    device.autoConnect = !map.contains(KEY_AUTO_CONNECT) || (map[KEY_AUTO_CONNECT] == "true");

    if (map.contains(KEY_PROFILE_ID)) {
        device.profileId = std::stoi(map[KEY_PROFILE_ID]);
    }
    return true;
}

bool save(const PairedDevice& device) {
    const auto addr_hex = addrToHex(device.addr);
    std::map<std::string, std::string> map;
    map[KEY_NAME]         = device.name;
    map[KEY_ADDR]         = addr_hex;
    map[KEY_AUTO_CONNECT] = device.autoConnect ? "true" : "false";
    map[KEY_PROFILE_ID]   = std::to_string(device.profileId);
    return file::savePropertiesFile(getFilePath(addr_hex), map);
}

bool remove(const std::string& addr_hex) {
    const auto file_path = getFilePath(addr_hex);
    if (!file::isFile(file_path)) return false;
    return ::remove(file_path.c_str()) == 0;
}

std::vector<PairedDevice> loadAll() {
    std::vector<dirent> entries;
    file::scandir(DATA_DIR, entries, [](const dirent* entry) -> int {
        if (entry->d_type != file::TT_DT_REG && entry->d_type != file::TT_DT_UNKNOWN) return -1;
        std::string name = entry->d_name;
        return name.ends_with(".device.properties") ? 0 : -1;
    }, nullptr);

    std::vector<PairedDevice> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        std::string filename = entry.d_name;
        constexpr std::string_view suffix = ".device.properties";
        if (filename.size() <= suffix.size()) continue;
        const std::string addr_hex = filename.substr(0, filename.size() - suffix.size());
        PairedDevice device;
        if (load(addr_hex, device)) {
            result.push_back(std::move(device));
        }
    }
    return result;
}

} // namespace tt::bluetooth::settings
