// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/module.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct Module app_hub_module;

/**
 * @brief Sets the root certificate that verifies the server of the official repository.
 * The other repositories are verified with the trusted root certificates of the platform.
 * @param[in] certificate_pem PEM text that must stay valid forever
 */
void app_hub_configure_certificate(const char* certificate_pem);

#ifdef __cplusplus
}
#endif
