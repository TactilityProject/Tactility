// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @return true when a network interface is connected, so connections to the internet can succeed.
 * Currently this is the case when a WiFi station is connected.
 */
bool network_is_available(void);

#ifdef __cplusplus
}
#endif
