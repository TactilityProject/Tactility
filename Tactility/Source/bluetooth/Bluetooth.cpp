#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/bluetooth/BluetoothPairedDevice.h>
#include <Tactility/bluetooth/BluetoothSettings.h>
#include <Tactility/bluetooth/BluetoothPrivate.h>

#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>
#include <Tactility/Tactility.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/bluetooth_hid_device.h>
#include <tactility/drivers/bluetooth_midi.h>
#include <tactility/drivers/bluetooth_serial.h>

#include <array>
#include <cstring>
#include <vector>

namespace tt::bluetooth {

static const auto LOGGER = Logger("Bluetooth");

// ---- Scan result cache (C++ PeerRecord list, updated from BT_EVENT_PEER_FOUND) ----

static Mutex scan_cache_mutex;
static std::vector<PeerRecord> scan_results_cache;

struct CachedAddr {
    uint8_t addr[6];
    uint8_t addr_type;
};
static std::vector<CachedAddr> scan_addr_cache; // parallel to scan_results_cache

// ---- Device accessor ----

struct Device* findFirstDevice() {
    struct Device* found = nullptr;
    device_for_each_of_type(&BLUETOOTH_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        if (device_is_ready(dev)) {
            *static_cast<struct Device**>(ctx) = dev;
            return false;
        }
        return true;
    });
    return found;
}

// ---- Scan cache helpers ----

void cacheScanAddr(const uint8_t addr[6], uint8_t addr_type) {
    auto lock = scan_cache_mutex.asScopedLock();
    lock.lock();
    for (auto& entry : scan_addr_cache) {
        if (memcmp(entry.addr, addr, 6) == 0) {
            entry.addr_type = addr_type;
            return;
        }
    }
    CachedAddr e = {};
    memcpy(e.addr, addr, 6);
    e.addr_type = addr_type;
    scan_addr_cache.push_back(e);
}

bool getCachedScanAddrType(const uint8_t addr[6], uint8_t* addr_type_out) {
    auto lock = scan_cache_mutex.asScopedLock();
    lock.lock();
    for (const auto& entry : scan_addr_cache) {
        if (memcmp(entry.addr, addr, 6) == 0) {
            if (addr_type_out) *addr_type_out = entry.addr_type;
            return true;
        }
    }
    if (addr_type_out) *addr_type_out = 0;
    return false;
}

static void cachePeerRecord(const BtPeerRecord& krecord) {
    PeerRecord rec;
    memcpy(rec.addr.data(), krecord.addr, 6);
    rec.name      = krecord.name[0] != '\0' ? krecord.name : "";
    rec.rssi      = krecord.rssi;
    rec.paired    = krecord.paired;
    rec.connected = krecord.connected;
    rec.profileId = 0;

    cacheScanAddr(krecord.addr, krecord.addr_type);

    auto lock = scan_cache_mutex.asScopedLock();
    lock.lock();
    for (auto& existing : scan_results_cache) {
        if (existing.addr == rec.addr) {
            if (!rec.name.empty()) existing.name = rec.name;
            existing.rssi = rec.rssi;
            return;
        }
    }
    scan_results_cache.push_back(std::move(rec));
}

// ---- Bridge callback (registered with kernel driver) ----
// This callback listens to platform driver events to perform auto-start logic
// and settings management. Consumers should register their own callbacks via
// bluetooth_add_event_callback() to receive events directly.

static void bt_event_bridge(struct Device* /*device*/, void* /*context*/, struct BtEvent event) {
    switch (event.type) {
        case BT_EVENT_RADIO_STATE_CHANGED:
            switch (event.radio_state) {
                case BT_RADIO_STATE_ON:
                    getMainDispatcher().dispatch([] {
                        auto peers = settings::loadAll();
                        bool has_hid_host_auto   = false;
                        bool has_hid_device_auto = false;
                        for (const auto& p : peers) {
                            if (!p.autoConnect) continue;
                            if (p.profileId == BT_PROFILE_HID_HOST)   has_hid_host_auto   = true;
                            if (p.profileId == BT_PROFILE_HID_DEVICE) has_hid_device_auto = true;
                        }
                        if (has_hid_host_auto) {
                            LOGGER.info("HID host auto-connect peer found — starting scan");
                            if (struct Device* dev = findFirstDevice()) {
                                bluetooth_scan_start(dev);
                            }
                        } else if (has_hid_device_auto) {
                            LOGGER.info("HID device auto-start (bonded peer found)");
                            if (struct Device* dev = bluetooth_hid_device_get_device()) {
                                bluetooth_hid_device_start(dev, BT_HID_DEVICE_MODE_KEYBOARD);
                            }
                        } else if (settings::shouldSppAutoStart()) {
                            LOGGER.info("Auto-starting SPP server");
                            if (struct Device* dev = bluetooth_serial_get_device()) {
                                bluetooth_serial_start(dev);
                            }
                        } else if (settings::shouldMidiAutoStart()) {
                            LOGGER.info("Auto-starting MIDI server");
                            if (struct Device* dev = bluetooth_midi_get_device()) {
                                bluetooth_midi_start(dev);
                            }
                        }
                    });
                    break;
                default:
                    break;
            }
            break;

        case BT_EVENT_SCAN_STARTED:
            {
                auto lock = scan_cache_mutex.asScopedLock();
                lock.lock();
                scan_results_cache.clear();
                scan_addr_cache.clear();
            }
            break;

        case BT_EVENT_SCAN_FINISHED:
            getMainDispatcher().dispatch([] { autoConnectHidHost(); });
            break;

        case BT_EVENT_PEER_FOUND:
            cachePeerRecord(event.peer);
            break;

        case BT_EVENT_PAIR_RESULT:
            if (event.pair_result.result == BT_PAIR_RESULT_SUCCESS) {
                uint8_t addr_buf[6];
                int profile_copy = event.pair_result.profile;
                memcpy(addr_buf, event.pair_result.addr, 6);
                getMainDispatcher().dispatch([addr_buf, profile_copy]() mutable {
                    std::array<uint8_t, 6> peer_addr;
                    memcpy(peer_addr.data(), addr_buf, 6);
                    const auto hex = settings::addrToHex(peer_addr);
                    if (!settings::hasFileForDevice(hex)) {
                        settings::PairedDevice dev;
                        dev.addr        = peer_addr;
                        dev.name        = "";
                        dev.autoConnect = true;
                        dev.profileId   = profile_copy;
                        if (settings::save(dev)) {
                            LOGGER.info("Saved paired peer {} (profile={})", hex, profile_copy);
                        }
                    }
                });
            } else if (event.pair_result.result == BT_PAIR_RESULT_BOND_LOST) {
                uint8_t addr_buf[6];
                memcpy(addr_buf, event.pair_result.addr, 6);
                getMainDispatcher().dispatch([addr_buf]() mutable {
                    std::array<uint8_t, 6> peer_addr;
                    memcpy(peer_addr.data(), addr_buf, 6);
                    settings::remove(settings::addrToHex(peer_addr));
                });
            }
            break;

        case BT_EVENT_PROFILE_STATE_CHANGED:
            if (event.profile_state.state == BT_PROFILE_STATE_CONNECTED) {
                uint8_t addr_buf[6];
                int profile_copy = (int)event.profile_state.profile;
                memcpy(addr_buf, event.profile_state.addr, 6);
                getMainDispatcher().dispatch([addr_buf, profile_copy]() mutable {
                    std::array<uint8_t, 6> peer_addr;
                    memcpy(peer_addr.data(), addr_buf, 6);
                    const auto hex = settings::addrToHex(peer_addr);
                    settings::PairedDevice stored;
                    if (settings::load(hex, stored) && stored.profileId != profile_copy) {
                        stored.profileId = profile_copy;
                        settings::save(stored);
                    }
                });
            } else if (event.profile_state.state == BT_PROFILE_STATE_IDLE &&
                       event.profile_state.profile == BT_PROFILE_HID_HOST) {
                // HID host disconnected — check if any peer has autoConnect and re-scan
                // so that autoConnectHidHost() fires when the scan finishes.
                getMainDispatcher().dispatch([] {
                    auto peers = settings::loadAll();
                    bool has_auto = false;
                    for (const auto& p : peers) {
                        if (p.autoConnect && p.profileId == BT_PROFILE_HID_HOST) {
                            has_auto = true;
                            break;
                        }
                    }
                    if (has_auto) {
                        if (struct Device* dev = findFirstDevice()) {
                            if (!bluetooth_is_scanning(dev)) {
                                bluetooth_scan_start(dev);
                            }
                        }
                    }
                });
            }
            break;

        default:
            break;
    }
}

// ---- systemStart ----

void systemStart() {
    struct Device* dev = findFirstDevice();
    if (dev == nullptr) {
        LOGGER.warn("systemStart: no BLE device found");
        return;
    }
    bluetooth_add_event_callback(dev, nullptr, bt_event_bridge);

    if (settings::shouldEnableOnBoot()) {
        LOGGER.info("Auto-enabling Bluetooth on boot");
        bluetooth_set_radio_enabled(dev, true);
    }
}

// ---- Public API ----

const char* radioStateToString(RadioState state) {
    switch (state) {
        using enum RadioState;
        case Off:        return "Off";
        case OnPending:  return "OnPending";
        case On:         return "On";
        case OffPending: return "OffPending";
    }
    check(false, "not implemented");
}

RadioState getRadioState() {
    struct Device* dev = findFirstDevice();
    if (dev == nullptr) return RadioState::Off;
    BtRadioState state = BT_RADIO_STATE_OFF;
    bluetooth_get_radio_state(dev, &state);
    switch (state) {
        case BT_RADIO_STATE_OFF:         return RadioState::Off;
        case BT_RADIO_STATE_ON_PENDING:  return RadioState::OnPending;
        case BT_RADIO_STATE_ON:          return RadioState::On;
        case BT_RADIO_STATE_OFF_PENDING: return RadioState::OffPending;
    }
    return RadioState::Off;
}

std::vector<PeerRecord> getScanResults() {
    auto lock = scan_cache_mutex.asScopedLock();
    lock.lock();
    return scan_results_cache;
}

std::vector<PeerRecord> getPairedPeers() {
    auto stored = settings::loadAll();
    std::vector<PeerRecord> result;
    result.reserve(stored.size());
    std::array<uint8_t, 6> connected_addr = {};
    bool hid_host_connected = hidHostGetConnectedPeer(connected_addr);
    for (const auto& device : stored) {
        PeerRecord record;
        record.addr      = device.addr;
        record.name      = device.name;
        record.rssi      = 0;
        record.paired    = true;
        record.profileId = device.profileId;
        record.connected = hid_host_connected && device.addr == connected_addr;
        result.push_back(std::move(record));
    }
    // Synthesize fallback: LittleFS readdir can lag behind fwrite by one tick, so the
    // connected peer may not appear in loadAll() yet. Always ensure it is in the list.
    if (hid_host_connected) {
        bool found = false;
        for (const auto& r : result) {
            if (r.addr == connected_addr) { found = true; break; }
        }
        if (!found) {
            PeerRecord record;
            record.addr      = connected_addr;
            record.rssi      = 0;
            record.paired    = true;
            record.connected = true;
            record.profileId = BT_PROFILE_HID_HOST;
            // Try to get the name from the scan cache.
            {
                auto lock = scan_cache_mutex.asScopedLock();
                lock.lock();
                for (const auto& sr : scan_results_cache) {
                    if (sr.addr == connected_addr) { record.name = sr.name; break; }
                }
            }
            result.push_back(std::move(record));
        }
    }
    return result;
}

void pair(const std::array<uint8_t, 6>& /*addr*/) {
    // Pairing is handled automatically during connection by NimBLE SM.
}

void unpair(const std::array<uint8_t, 6>& addr) {
    struct Device* dev = findFirstDevice();
    if (dev != nullptr) {
        bluetooth_unpair(dev, addr.data());
    }
    settings::remove(settings::addrToHex(addr));
}

void connect(const std::array<uint8_t, 6>& addr, int profileId) {
    LOGGER.info("connect(profile={})", profileId);
    if (profileId == BT_PROFILE_HID_HOST) {
        hidHostConnect(addr);
    } else if (profileId == BT_PROFILE_HID_DEVICE) {
        if (struct Device* dev = bluetooth_hid_device_get_device()) {
            bluetooth_hid_device_start(dev, BT_HID_DEVICE_MODE_KEYBOARD);
        }
    } else if (profileId == BT_PROFILE_SPP) {
        if (struct Device* dev = bluetooth_serial_get_device()) {
            bluetooth_serial_start(dev);
            settings::setSppAutoStart(true);
        }
    } else if (profileId == BT_PROFILE_MIDI) {
        if (struct Device* dev = bluetooth_midi_get_device()) {
            bluetooth_midi_start(dev);
            settings::setMidiAutoStart(true);
        }
    }
}

void disconnect(const std::array<uint8_t, 6>& addr, int profileId) {
    LOGGER.info("disconnect(profile={})", profileId);
    if (profileId == BT_PROFILE_HID_HOST) {
        hidHostDisconnect();
    } else if (profileId == BT_PROFILE_HID_DEVICE) {
        if (struct Device* dev = bluetooth_hid_device_get_device()) {
            bluetooth_hid_device_stop(dev);
        }
    } else {
        struct Device* dev = findFirstDevice();
        if (dev == nullptr) return;
        bluetooth_disconnect(dev, addr.data(), (BtProfileId)profileId);
    }
}

bool isProfileSupported(int profileId) {
    return profileId == BT_PROFILE_HID_HOST ||
           profileId == BT_PROFILE_HID_DEVICE ||
           profileId == BT_PROFILE_SPP ||
           profileId == BT_PROFILE_MIDI;
}

} // namespace tt::bluetooth

#endif // CONFIG_BT_NIMBLE_ENABLED
