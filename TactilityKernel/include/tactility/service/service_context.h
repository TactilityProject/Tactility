// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <tactility/service/service_instance.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The context handed to a service's on_start/on_stop callbacks.
 * This is the same object as the ServiceInstance that owns the service (C has no
 * interface/inheritance to distinguish a restricted "context" view from the full
 * instance, so the two are deliberately the same type under different names).
 */
typedef struct ServiceInstance ServiceContext;

/**
 * @brief Get the manifest of the running service.
 * @param[in] context non-null service context pointer
 * @return the manifest
 */
const struct ServiceManifest* service_context_get_manifest(ServiceContext* context);

#ifdef __cplusplus
}
#endif
