// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#include <tactility/drivers/bluetooth.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct BluetoothPeer {
    BtAddr addr;
    char name[BT_NAME_MAX + 1];
    int8_t rssi;
    bool paired;
    bool connected;
    /** Profile used to pair (BtProfileId value). Only meaningful for paired peers. */
    int profile;
};

/**
 * Calls on_peer for every peer found during the last scan.
 * @warning The service is locked during the call: on_peer must not call other bluetooth_service functions.
 * @param[in] on_peer return false to stop iterating
 */
void bluetooth_service_for_each_scan_result(void* context, bool (*on_peer)(const struct BluetoothPeer* peer, void* context));

/**
 * Calls on_peer for every paired peer.
 * @param[in] on_peer return false to stop iterating
 */
void bluetooth_service_for_each_paired_peer(void* context, bool (*on_peer)(const struct BluetoothPeer* peer, void* context));

/**
 * Turns the radio on or off. Unlike calling the driver directly, a radio that is turned on this way is managed by the service:
 * it scans periodically and starts the profiles that are configured to start automatically.
 * The radio that is turned on at boot is managed too.
 */
error_t bluetooth_service_set_radio_enabled(bool enabled);

/** Connects to a peer. For the HID host profile the result is delivered as BT_EVENT_PROFILE_STATE_CHANGED. */
void bluetooth_service_connect(const BtAddr addr, enum BtProfileId profile);

void bluetooth_service_disconnect(const BtAddr addr, enum BtProfileId profile);

/** Removes the bond with the peer and forgets the saved device. */
void bluetooth_service_unpair(const BtAddr addr);

#ifdef __cplusplus
}
#endif
