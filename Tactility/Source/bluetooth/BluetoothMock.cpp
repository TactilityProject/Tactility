#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if !defined(CONFIG_BT_NIMBLE_ENABLED)

#include <Tactility/bluetooth/Bluetooth.h>

namespace tt::bluetooth {

struct Device* findFirstDevice()                    { return nullptr; }

const char* radioStateToString(RadioState state) {
    switch (state) {
        using enum RadioState;
        case Off:        return "Off";
        case OnPending:  return "OnPending";
        case On:         return "On";
        case OffPending: return "OffPending";
    }
    return "Unknown";
}

RadioState getRadioState()                    { return RadioState::Off; }

std::vector<PeerRecord> getScanResults()      { return {}; }
std::vector<PeerRecord> getPairedPeers()      { return {}; }

void pair(const std::array<uint8_t, 6>& /*addr*/)              {}
void unpair(const std::array<uint8_t, 6>& /*addr*/)            {}
void connect(const std::array<uint8_t, 6>& /*addr*/, int /*profileId*/)    {}
void disconnect(const std::array<uint8_t, 6>& /*addr*/, int /*profileId*/) {}

bool isProfileSupported(int /*profileId*/)    { return false; }

void hidHostConnect(const std::array<uint8_t, 6>& /*addr*/) {}
void hidHostDisconnect()                      {}
bool hidHostIsConnected()                     { return false; }

void systemStart() {}

} // namespace tt::bluetooth

#endif // !CONFIG_BT_NIMBLE_ENABLED
