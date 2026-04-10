#pragma once

#include <Tactility/bluetooth/Bluetooth.h>

#include <tactility/drivers/bluetooth.h>

struct Device;

namespace tt::bluetooth {

/** Cache a BLE address and its type from a scan result (used by HID host for addr_type lookup). */
void cacheScanAddr(const uint8_t addr[6], uint8_t addr_type);

/**
 * Look up the ble_addr_t (including address type) for a peer address in the last scan.
 * Returns false and fills type=0 (PUBLIC) if not found.
 */
bool getCachedScanAddrType(const uint8_t addr[6], uint8_t* addr_type_out);

/** Called from BluetoothHidHost.cpp to trigger auto-connect check after scan finishes. */
void autoConnectHidHost();

/**
 * Returns the BLE address of the currently fully-connected HID host peer (i.e.
 * all CCCDs subscribed and ready). Returns false if no HID host peer is connected.
 */
bool hidHostGetConnectedPeer(std::array<uint8_t, 6>& addr_out);

} // namespace tt::bluetooth
