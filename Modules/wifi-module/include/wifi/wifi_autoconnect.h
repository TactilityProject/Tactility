// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pause auto-connect until the next connection attempt finishes or the radio is turned on.
 * Call this before a user-initiated disconnect, so auto-connect doesn't immediately reconnect.
 */
void wifi_autoconnect_pause_until_connected(void);

#ifdef __cplusplus
}
#endif
