// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/package_manifest.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * App Hub service: finds, installs and removes apps from the online app catalog.
 * Operations are queued and run one at a time, in order, on a background task. Their outcome is reported through events.
 */

#define APP_HUB_NAME_MAX_LENGTH 63

enum AppHubOperationType {
    /** Download the app catalog */
    APP_HUB_OPERATION_REFRESH,
    /** Download and install an app from the catalog, replacing an installed version */
    APP_HUB_OPERATION_INSTALL,
    /** Uninstall an app */
    APP_HUB_OPERATION_REMOVE,
    /** Install the newer catalog version of every installed app that has one */
    APP_HUB_OPERATION_UPDATE_ALL,
};

/** An app from the catalog, an installed app, or both */
struct AppHubApp {
    PackageId id;
    /** The catalog name, or the name of the package's first app when it isn't in the catalog */
    char name[APP_HUB_NAME_MAX_LENGTH + 1];
    /** The catalog version, empty when the app isn't in the catalog */
    char version_name[PACKAGE_MANIFEST_VERSION_NAME_LENGTH + 1];
    uint64_t version_code;
    /** The installed version, empty when the app isn't installed */
    char installed_version_name[PACKAGE_MANIFEST_VERSION_NAME_LENGTH + 1];
    uint64_t installed_version_code;
    bool in_catalog;
    bool installed;
    /** The catalog has a newer version than the installed one */
    bool update_available;
    /** An install or remove operation for this app is queued or running */
    bool operation_pending;
    /** The pending operation, valid when operation_pending is set */
    enum AppHubOperationType pending_operation;
};

/**
 * @param[in] app the app, only valid during the call
 * @return true to continue, false to stop
 */
typedef bool (*AppHubAppVisitorFn)(const struct AppHubApp* app, void* context);

// region Apps

/**
 * Visit the catalog apps that are compatible with this device and all installed apps, sorted by name.
 * @warning The visitor runs with the service's lock held: it must not call app_hub_*() functions.
 * @param[in] query NULL or empty for all apps, otherwise a case-insensitive part of the name or id
 * @param[in] visitor called for every matching app
 * @param[in] context passed to @a visitor
 */
error_t app_hub_search(const char* query, AppHubAppVisitorFn visitor, void* context);

/**
 * @param[in] id the app id, must not be NULL
 * @param[out] app the app, must not be NULL
 * @retval ERROR_NOT_FOUND when the app is neither in the catalog nor installed
 */
error_t app_hub_get_app(const char* id, struct AppHubApp* app);

/**
 * Get the catalog description of an app.
 * @param[in] id the app id, must not be NULL
 * @param[out] description the description, must not be NULL
 * @param[in] description_size the size of @a description
 * @retval ERROR_NOT_FOUND when the app isn't in the catalog
 * @retval ERROR_BUFFER_OVERFLOW when @a description_size is too small
 */
error_t app_hub_get_description(const char* id, char* description, size_t description_size);

// endregion

// region Operations

/** @return true while an operation is queued or running */
bool app_hub_is_busy(void);

/**
 * Queue a download of the app catalog.
 * Only one refresh is queued or running at a time: while one is pending, its id is returned instead.
 * @param[out] operation_id the id of the queued operation (optional)
 */
error_t app_hub_refresh(uint32_t* operation_id);

/**
 * Queue the installation of a catalog app. An installed version is replaced.
 * Installs and removes of different apps can be queued together. Per app, only one is queued or running at a time.
 * @param[out] operation_id the id of the queued operation (optional)
 * @retval ERROR_NONE the operation was queued, or the same install was already pending and its id is returned
 * @retval ERROR_RESOURCE_BUSY a remove of this app or an update of all apps is pending
 */
error_t app_hub_install(const char* id, uint32_t* operation_id);

/**
 * Queue the update of every installed app that has a newer version in the catalog. The apps are installed one at a time,
 * with an INSTALLING and INSTALLED event each. A failed update doesn't stop the updates of the other apps.
 * While the operation is pending, the apps with an update report it as their pending operation.
 * It can't be queued next to an install or remove.
 * @param[out] operation_id the id of the queued operation (optional)
 * @retval ERROR_NONE the operation was queued, or it was already pending and its id is returned
 * @retval ERROR_RESOURCE_BUSY an install or remove is pending
 */
error_t app_hub_update_all(uint32_t* operation_id);

/**
 * Queue the removal of an installed app.
 * Installs and removes of different apps can be queued together. Per app, only one is queued or running at a time.
 * @param[out] operation_id the id of the queued operation (optional)
 * @retval ERROR_NONE the operation was queued, or the same removal was already pending and its id is returned
 * @retval ERROR_RESOURCE_BUSY an install of this app or an update of all apps is pending
 */
error_t app_hub_remove(const char* id, uint32_t* operation_id);

// endregion

// region Events

enum AppHubEventType {
    /** The catalog download started */
    APP_HUB_EVENT_REFRESHING,
    /** The catalog download finished, see AppHubEvent.result */
    APP_HUB_EVENT_REFRESHED,
    /** The installation of AppHubEvent.app_id started */
    APP_HUB_EVENT_INSTALLING,
    /** The installation of AppHubEvent.app_id finished, see AppHubEvent.result */
    APP_HUB_EVENT_INSTALLED,
    /** The removal of AppHubEvent.app_id started */
    APP_HUB_EVENT_REMOVING,
    /** The removal of AppHubEvent.app_id finished, see AppHubEvent.result */
    APP_HUB_EVENT_REMOVED,
    /** Updating all apps started. INSTALLING and INSTALLED events with the same operation id follow for each app. */
    APP_HUB_EVENT_UPDATING,
    /** Updating all apps finished. AppHubEvent.result is the error of the last failed app, ERROR_NONE when all succeeded. */
    APP_HUB_EVENT_UPDATED,
};

struct AppHubEvent {
    enum AppHubEventType type;
    uint32_t operation_id;
    /** Empty for the refresh, UPDATING and UPDATED events */
    PackageId app_id;
    /** The outcome of the operation for the REFRESHED, INSTALLED and REMOVED events, ERROR_NONE otherwise */
    error_t result;
};

#define APP_HUB_EVENT_QUEUE_CAPACITY 8

/**
 * Caller-owned subscription: wait on the event group, then drain with app_hub_event_poll().
 * The subscriber must poll often enough: while APP_HUB_EVENT_QUEUE_CAPACITY events are queued, newer events are dropped.
 * @warning Fields other than `bit` are for internal use only.
 */
struct AppHubEventSubscription {
    uint32_t bit;

    struct {
        struct TaskEventGroup* event_group;
        struct AppHubEvent queue[APP_HUB_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;
        struct AppHubEventSubscription* next;
    } internal;
};

/**
 * @param[in,out] sub subscription to register, must stay alive until unsubscribed
 * @param[in] event_group caller-owned group to wait on, must outlive @a sub
 * @retval ERROR_RESOURCE @a event_group has no free bits left
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t app_hub_event_subscribe(struct AppHubEventSubscription* sub, struct TaskEventGroup* event_group);

error_t app_hub_event_unsubscribe(struct AppHubEventSubscription* sub);

/**
 * Block until an event is queued for the subscription, or the timeout elapses.
 * @param[in] sub a registered subscription
 * @param[in] timeout max ticks to wait
 * @retval ERROR_NONE an event can be polled
 * @retval ERROR_TIMEOUT no event arrived in time
 */
error_t app_hub_event_await(struct AppHubEventSubscription* sub, TickType_t timeout);

/**
 * Non-blocking: pop the next event.
 * @retval ERROR_TIMEOUT nothing queued
 */
error_t app_hub_event_poll(struct AppHubEventSubscription* sub, struct AppHubEvent* event);

// endregion

#ifdef __cplusplus
}
#endif
