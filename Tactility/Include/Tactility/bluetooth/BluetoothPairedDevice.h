#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tt::bluetooth::settings {

struct PairedDevice {
    std::string name;
    std::array<uint8_t, 6> addr;
    bool autoConnect = false;
    /** Profile used to pair (BtProfileId value). Defaults to BT_PROFILE_SPP=2. */
    int profileId = 2;
};

std::string addrToHex(const std::array<uint8_t, 6>& addr);

bool hasFileForDevice(const std::string& addr_hex);

bool load(const std::string& addr_hex, PairedDevice& device);

bool save(const PairedDevice& device);

bool remove(const std::string& addr_hex);

std::vector<PairedDevice> loadAll();

} // namespace tt::bluetooth::settings
