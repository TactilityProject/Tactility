// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <esp_wifi_types_generic.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shared ownership over the single ESP32 WiFi radio's esp_wifi_init()/esp_wifi_start() lifecycle.
 * Station mode (esp32_wifi driver) and the WebServer's Access Point mode both need the radio and
 * must not blindly call esp_wifi_init()/esp_wifi_deinit() themselves - doing so unconditionally
 * breaks whichever owner got there second (failing outright with ESP_ERR_INVALID_STATE, or having
 * its radio torn out from under it when the other side releases). Callers acquire the mode bit they
 * need (WIFI_MODE_STA or WIFI_MODE_AP); the radio is brought up on the first acquire (merging into
 * WIFI_MODE_APSTA if another owner is already active) and only torn down once every owner has
 * released.
 *
 * @param mode_bit WIFI_MODE_STA or WIFI_MODE_AP (not WIFI_MODE_APSTA/WIFI_MODE_NULL).
 * @retval true the mode bit is now active (radio is initialized, started, and its current mode
 *     includes mode_bit) - the caller may proceed to configure and use its interface.
 * @retval false failed to bring up or merge into the requested mode; the radio's state is
 *     unchanged from before this call.
 */
bool esp32_wifi_radio_acquire(wifi_mode_t mode_bit);

/**
 * Releases a previously-acquired mode bit. If this was the last owner (resulting mode would be
 * WIFI_MODE_NULL), fully stops and deinitializes the radio; otherwise just drops that bit from the
 * active mode, leaving the radio running for the remaining owner.
 *
 * @warning Must be called with the same mode_bit passed to a prior successful acquire() - never
 *     call this after a failed acquire().
 */
void esp32_wifi_radio_release(wifi_mode_t mode_bit);

#ifdef __cplusplus
}
#endif
