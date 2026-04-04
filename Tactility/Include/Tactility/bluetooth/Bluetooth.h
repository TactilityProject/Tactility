#pragma once

#include <array>
#include <string>
#include <vector>
#include <cstdint>

struct Device;

namespace tt::bluetooth {

enum class RadioState {
    Off,
    OnPending,
    On,
    OffPending,
};

struct PeerRecord {
    std::array<uint8_t, 6> addr;
    std::string name;
    int8_t rssi;
    bool paired;
    bool connected;
    /** Profile used to pair (BtProfileId value). Only meaningful for paired peers. */
    int profileId = 0;
};

/** Find the first ready BLE device in the kernel device registry. Returns nullptr if unavailable. */
struct Device* findFirstDevice();

/** @return the current radio state */
RadioState getRadioState();

/** For logging purposes */
const char* radioStateToString(RadioState state);

/** @return the peers found during the last scan */
std::vector<PeerRecord> getScanResults();

/** @return the list of currently paired peers */
std::vector<PeerRecord> getPairedPeers();

/**
 * @brief Initiate pairing with a peer.
 * Returns immediately; result is delivered via kernel event callback (BT_EVENT_PAIR_RESULT).
 */
void pair(const std::array<uint8_t, 6>& addr);

/**
 * @brief Remove a previously paired peer.
 * @param[in] addr the peer address
 */
void unpair(const std::array<uint8_t, 6>& addr);

/**
 * @brief Connect to a peer using the specified profile.
 * @param[in] addr the peer address
 * @param[in] profileId the BtProfileId value (from bluetooth.h)
 */
void connect(const std::array<uint8_t, 6>& addr, int profileId);

/**
 * @brief Disconnect a peer from the specified profile.
 * @param[in] addr the peer address
 * @param[in] profileId the BtProfileId value (from bluetooth.h)
 */
void disconnect(const std::array<uint8_t, 6>& addr, int profileId);

/**
 * @brief Check whether a given profile is supported on this build/SOC.
 * @param[in] profileId the BtProfileId value to query
 * @return true when the profile is available
 */
bool isProfileSupported(int profileId);

// ---- BLE HID Host (central role — connect to external BLE keyboard/mouse) ----

/**
 * @brief Connect to a remote BLE HID device (keyboard, mouse, etc.) as a host.
 * Discovery, CCCD subscription, and LVGL indev registration happen automatically.
 * @param[in] addr 6-byte BLE address of the HID peripheral
 */
void hidHostConnect(const std::array<uint8_t, 6>& addr);

/** @brief Disconnect from the currently connected BLE HID device. */
void hidHostDisconnect();

/** @return true when a BLE HID peripheral is fully subscribed and acting as LVGL input device */
bool hidHostIsConnected();

/**
 * @brief Initialize the Bluetooth bridge layer and optionally enable the radio.
 * Called once from Tactility startup (after kernel drivers are ready).
 * Reads settings and enables the radio if configured to auto-start.
 */
void systemStart();

} // namespace tt::bluetooth
