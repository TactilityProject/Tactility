#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <tactility/drivers/bluetooth.h>
#include <tactility/error.h>

struct Device;

// Starts the worker that creates and destroys the keyboard and mouse devices of a connected peer.
// device must be the root BLE device.
error_t ble_hid_host_init(struct Device* device);
void ble_hid_host_deinit();

// Connects to a BLE HID peripheral as central and exposes it as KEYBOARD_TYPE and MOUSE_TYPE devices.
error_t ble_hid_host_connect(struct Device* device, const BtAddr addr);
error_t ble_hid_host_disconnect();

#endif // CONFIG_BT_NIMBLE_ENABLED
