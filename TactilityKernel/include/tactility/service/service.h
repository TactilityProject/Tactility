// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

// ServiceContext (declared in service_context.h) is a typedef alias of ServiceInstance,
// so callbacks below are declared directly in terms of ServiceInstance to avoid a
// conflicting forward-declaration of an unrelated "ServiceContext" struct tag.
struct ServiceInstance;

/**
 * A service is a long-running background process (e.g. Wi-Fi, GPS, GUI).
 * Concrete services keep their own state behind the `data` pointer.
 */
struct Service {
    /** Service-specific data, owned by the service implementation. Can be NULL. */
    void* data;
    /**
     * Called when the service is starting.
     * Can be NULL, in which case starting always succeeds.
     * @param[in,out] service this service
     * @param[in,out] context the context (a ServiceInstance) for this running service
     * @return ERROR_NONE if the service started successfully
     */
    error_t (*on_start)(struct Service* service, struct ServiceInstance* context);
    /**
     * Called when the service is stopping.
     * Can be NULL, in which case stopping is a no-op.
     * @param[in,out] service this service
     * @param[in,out] context the context (a ServiceInstance) for this running service
     */
    void (*on_stop)(struct Service* service, struct ServiceInstance* context);
};

#ifdef __cplusplus
}
#endif
