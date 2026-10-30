// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Location service. Location providers (e.g. GPS) report fixes to the service.
 * The most recent fix of a regular provider is used while it's less than 2 minutes old.
 * Otherwise, the most recent fix of a fallback provider (e.g. IP geolocation) is used, if there is one.
 * Providers only run while there is at least one subscription.
 */

#define LOCATION_PROVIDER_ID_MAX_LENGTH 15

/** Maximum length of an IANA time zone name from location_lookup_time_zone(), excluding the NUL terminator */
#define LOCATION_TIME_ZONE_MAX_LENGTH 63

/** Horizontal accuracy in meters of a fix with LOCATION_PRECISION_APPROXIMATE */
#define LOCATION_APPROXIMATE_ACCURACY_METERS 3000.0f

enum LocationPrecision {
    /** Providers only report fixes with a horizontal accuracy of LOCATION_APPROXIMATE_ACCURACY_METERS or worse */
    LOCATION_PRECISION_APPROXIMATE,
    /** Providers report fixes as accurate as they can */
    LOCATION_PRECISION_PRECISE,
};

/**
 * A location fix. Values the provider doesn't know are NAN for floating point fields and 0 for the others.
 * Accuracies are the radius of 68% confidence, in the unit of the value they belong to.
 */
struct Location {
    /** Degrees in WGS84, positive is north */
    double latitude;
    /** Degrees in WGS84, positive is east */
    double longitude;
    /** Meters */
    float horizontal_accuracy;
    /** Meters above mean sea level */
    float altitude;
    /** Meters above the WGS84 ellipsoid */
    float ellipsoidal_altitude;
    /** Meters */
    float vertical_accuracy;
    /** Speed over ground in meters per second */
    float speed;
    /** Meters per second */
    float speed_accuracy;
    /** Direction of travel in degrees clockwise from true north, in range [0, 360) */
    float course;
    /** Degrees */
    float course_accuracy;
    /** Number of satellites used for the fix */
    uint8_t satellites;
    /** Time of the fix according to the provider, in milliseconds since the Unix epoch */
    int64_t time_ms;
    /** Milliseconds since boot when the service received the fix */
    uint64_t uptime_ms;
    /** Id of the provider that reported the fix */
    char provider[LOCATION_PROVIDER_ID_MAX_LENGTH + 1];
};

/**
 * Allow or disallow the use of location services. The service only runs while allowed. The choice is persisted.
 * Location services are disallowed by default.
 */
error_t location_set_enabled(bool enabled);

bool location_is_enabled(void);

/** Set the precision that providers should report. The choice is persisted. */
error_t location_set_precision(enum LocationPrecision precision);

enum LocationPrecision location_get_precision(void);

/**
 * Get the current fix. Without subscriptions, this is the last known fix.
 * @param[out] location the fix
 * @retval ERROR_NOT_FOUND when no fix was reported since the service started, or location services are disabled
 */
error_t location_get(struct Location* location);

#define LOCATION_SUBSCRIBER_DESCRIPTION_MAX_LENGTH 24

/**
 * Caller-owned subscription: the service signals @a bit on the event group whenever a new current fix is reported.
 * Read the fix with location_get().
 * @warning Fields other than `bit` are for internal use only.
 */
struct LocationSubscription {
    uint32_t bit;

    struct {
        char description[LOCATION_SUBSCRIBER_DESCRIPTION_MAX_LENGTH + 1];
        struct TaskEventGroup* event_group;
        struct LocationSubscription* next;
    } internal;
};

/**
 * Subscribing starts the providers when location services are enabled. They stop when the last subscription is removed.
 * Providers start and stop in the background: subscribing and unsubscribing don't wait for them.
 * @param[in,out] sub subscription to register, must stay alive until unsubscribed
 * @param[in] event_group caller-owned group to wait on, must outlive @a sub
 * @param[in] description what the subscriber is, shown to the user. Truncated to LOCATION_SUBSCRIBER_DESCRIPTION_MAX_LENGTH characters.
 * @retval ERROR_RESOURCE @a event_group has no free bits left
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t location_subscribe(struct LocationSubscription* sub, struct TaskEventGroup* event_group, const char* description);

/** @retval ERROR_NOT_FOUND when @a sub is not registered */
error_t location_unsubscribe(struct LocationSubscription* sub);

/**
 * Calls @a callback for each subscription, in subscription order.
 * The service lock is held during the callback, so it must only copy the description.
 */
void location_for_each_subscriber(void* context, void (*callback)(const char* description, void* context));

/** @return true when location services are enabled and there is at least one subscription */
bool location_is_in_use(void);

/**
 * Look up the IANA time zone name (e.g. "Europe/Brussels") of the device's public IP address with json.geoiplookup.io.
 * Blocks for up to about 15 seconds, so call it from an app's own task.
 * @param[out] out_name the time zone name
 * @param[in] out_size the size of @a out_name, at least LOCATION_TIME_ZONE_MAX_LENGTH + 1 for any name
 * @retval ERROR_INVALID_STATE location services are disabled
 * @retval ERROR_RESOURCE the request failed, e.g. without network
 * @retval ERROR_NOT_FOUND json.geoiplookup.io returned no time zone
 * @retval ERROR_BUFFER_OVERFLOW the name doesn't fit in @a out_name
 */
error_t location_lookup_time_zone(char* out_name, size_t out_size);

#ifdef __cplusplus
}
#endif
