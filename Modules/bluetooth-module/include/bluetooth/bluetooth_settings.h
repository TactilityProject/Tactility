// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sets whether the Bluetooth radio is turned on at boot. */
error_t bluetooth_settings_set_enable(bool enable);
bool bluetooth_settings_get_enable(void);

/** Sets whether the SPP server is started when the radio turns on. */
error_t bluetooth_settings_set_spp_auto_start(bool enable);
bool bluetooth_settings_get_spp_auto_start(void);

/** Sets whether the MIDI server is started when the radio turns on. */
error_t bluetooth_settings_set_midi_auto_start(bool enable);
bool bluetooth_settings_get_midi_auto_start(void);

#ifdef __cplusplus
}
#endif
