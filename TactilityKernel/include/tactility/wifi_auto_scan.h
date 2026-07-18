// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Suspend or resume WifiService's periodic background auto-connect scan.
 *
 * Not a device operation - see tactility/drivers/wifi.h for the actual radio API. This exists
 * for narrow, short-lived windows where any WiFi/co-processor traffic would be unsafe (e.g. a
 * known co-processor reboot in progress during an OTA update) - callers must resume when done.
 * Independent of WifiService's own internal connect()/disconnect() pause bookkeeping: it is
 * never cleared implicitly by a connection succeeding/failing or the radio being enabled, only a
 * matching wifi_auto_scan_set_paused(false) clears it.
 *
 * The real implementation lives in the Tactility WiFi service
 * (Tactility/Source/service/wifi/Wifi.cpp), a layer above TactilityKernel - TactilityKernel
 * can't call up into it directly (and mustn't link against it: TactilityKernelTests links
 * TactilityKernel alone, without Tactility). Tactility registers its implementation at startup
 * via wifi_auto_scan_register(); until then (or on a build that never links Tactility, e.g. a
 * bare-kernel target) this is a no-op, same as WifiApi::get_firmware_ops() returning
 * ERROR_NOT_SUPPORTED when nothing is registered.
 *
 * @param[in] paused when true, the auto-connect timer stops issuing scans until resumed
 */
void wifi_auto_scan_set_paused(bool paused);

/** @brief Register the real implementation. Called once by the Tactility WiFi service. */
void wifi_auto_scan_register(void (*set_paused)(bool paused));

#ifdef __cplusplus
}
#endif
