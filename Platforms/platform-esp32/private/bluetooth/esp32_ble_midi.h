#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_BT_NIMBLE_ENABLED)

#include <tactility/drivers/bluetooth_midi.h>
#include <tactility/error.h>

#include <esp_timer.h>
#include <cstdint>

struct Device;

bool     ble_midi_get_active(struct Device* device);
void     ble_midi_set_active(struct Device* device, bool v);
uint16_t ble_midi_get_conn_handle(struct Device* device);
void     ble_midi_set_conn_handle(struct Device* device, uint16_t h);
bool     ble_midi_get_use_indicate(struct Device* device);
void     ble_midi_set_use_indicate(struct Device* device, bool v);

// MIDI keepalive timer helpers — timer handle lives in BleCtx.
// ble_midi_ensure_keepalive creates the timer if needed and starts it periodically.
// ble_midi_stop_keepalive stops (but does not delete) the timer.
error_t ble_midi_ensure_keepalive(struct Device* device, esp_timer_cb_t cb, uint64_t period_us);
void    ble_midi_stop_keepalive(struct Device* device);


#endif // CONFIG_BT_NIMBLE_ENABLED
