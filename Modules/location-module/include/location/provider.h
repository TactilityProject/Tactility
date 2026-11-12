// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <location/location.h>

#include <tactility/freertos/freertos.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Interface between the location service and a location provider. Not part of the app SDK.
 */

/** The service, as seen by a provider */
struct LocationHost;

struct LocationProviderApi {
    /** Unique id, at most LOCATION_PROVIDER_ID_MAX_LENGTH characters */
    const char* id;

    /**
     * A fallback provider (e.g. IP geolocation) is only used while no other provider reported a fix in the last
     * 2 minutes. It should only work when location_host_needs_fallback() returns true.
     */
    bool fallback;

    /**
     * True when the provider reports the time of its fixes from a reliable clock, e.g. satellites.
     * The service announces that time with KERNEL_EVENT_LOCATION_TIME.
     */
    bool provides_time;

    /**
     * Start the provider. Called on the service's worker task while the service runs and has subscriptions.
     * It must not block: the work happens on the provider's own task.
     * @return the provider state, NULL on failure
     */
    void* (*create)(struct LocationHost* host);

    /** Ask the provider to stop. It must not block. */
    void (*request_stop)(void* provider);

    /**
     * Wait at most @a timeout for the provider to stop and release its resources.
     * It must not report anymore once this returned true. The provider is only created again after that.
     * @return true when stopped
     */
    bool (*wait_stopped)(void* provider, TickType_t timeout);
};

/** Maximum number of registered providers */
#define LOCATION_PROVIDER_MAX_COUNT 4

/**
 * Register a provider. Usually called from the provider module's start().
 * @retval ERROR_ALREADY_EXISTS when a provider with the same id is registered
 * @retval ERROR_RESOURCE when LOCATION_PROVIDER_MAX_COUNT providers are registered
 */
error_t location_provider_register(const struct LocationProviderApi* api);

/** @retval ERROR_NOT_FOUND when @a api is not registered */
error_t location_provider_unregister(const struct LocationProviderApi* api);

/**
 * Report a new fix. Can be called from any task.
 * The service sets the `provider` and `uptime_ms` fields.
 */
void location_host_report(struct LocationHost* host, const struct Location* location);

/**
 * Reduce a fix to LOCATION_PRECISION_APPROXIMATE: the position snaps to a grid and only position and time remain.
 * Providers that can report precise fixes apply this when location_get_precision() is LOCATION_PRECISION_APPROXIMATE.
 */
void location_make_approximate(struct Location* location);

/** @return true when no provider that isn't a fallback reported a fix in the last 2 minutes */
bool location_host_needs_fallback(void);

#ifdef __cplusplus
}
#endif
