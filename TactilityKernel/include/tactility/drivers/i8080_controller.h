// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generic device type for i8080 (8080-series parallel) LCD bus controllers.
 *
 * Unlike SPI_CONTROLLER_TYPE, there is no locking API: an i80 bus only ever has one
 * consumer (a single display panel) in every known board, so no arbitration is needed.
 */
extern const struct DeviceType I8080_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
