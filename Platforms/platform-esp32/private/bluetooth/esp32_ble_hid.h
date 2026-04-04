#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <tactility/drivers/bluetooth_hid_device.h>

#include <cstdint>

enum BleHidProfile { None, KbConsumer, Mouse, KbMouse, Gamepad };

struct Device;

bool     ble_hid_get_active(struct Device* device);
void     ble_hid_set_active(struct Device* device, bool v);
uint16_t ble_hid_get_conn_handle(struct Device* device);
void     ble_hid_set_conn_handle(struct Device* device, uint16_t h);

// device must be the hid_device child Device*.
void ble_hid_init_gatt();
void ble_hid_switch_profile(struct Device* hid_child, BleHidProfile profile);

#endif // CONFIG_BT_NIMBLE_ENABLED
