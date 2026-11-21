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
#define APP_HUB_SHORT_DESCRIPTION_MAX_LENGTH 32

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
    /** The catalog's short description, empty when the app isn't in the catalog */
    char short_description[APP_HUB_SHORT_DESCRIPTION_MAX_LENGTH + 1];
    /** False when the catalog says that the app has no icon. Only meaningful for apps in the catalog. */
    bool has_icon;
    /** The catalog version code, 0 when the app isn't in the catalog */
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
 * Visit the catalog apps and all installed apps, sorted by name.
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

/** What the details of a catalog app have next to its description */
struct AppHubAppDetails {
    /** The catalog version */
    char version_name[PACKAGE_MANIFEST_VERSION_NAME_LENGTH + 1];
    /** This device can install and run the app */
    bool compatible;
};

/**
 * Download the details of a catalog app. Blocks until the download finishes, so it must not be called on the LVGL thread.
 * @param[in] id the app id, must not be NULL
 * @param[out] description the description, must not be NULL
 * @param[in] description_size the size of @a description
 * @param[out] details the version name and whether this device can install and run the app, must not be NULL
 * @retval ERROR_NOT_FOUND when the app isn't in the catalog
 * @retval ERROR_BUFFER_OVERFLOW when @a description_size is too small
 * @retval ERROR_RESOURCE when the download failed
 */
error_t app_hub_get_details(const char* id, char* description, size_t description_size, struct AppHubAppDetails* details);

/**
 * Get the path of the icon of a catalog app, which is downloaded when it isn't cached yet.
 * Blocks until the download finishes, so it must not be called on the LVGL thread.
 * @param[in] id the app id, must not be NULL
 * @param[in] mono true for the monochrome icon, false for the colour icon
 * @param[out] path the path of the SVG file, must not be NULL
 * @param[in] path_size the size of @a path
 * @retval ERROR_NOT_FOUND when the app isn't in the catalog
 * @retval ERROR_RESOURCE when the app has no such icon or the download failed
 */
error_t app_hub_get_icon(const char* id, bool mono, char* path, size_t path_size);

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

// region Repositories

#define APP_HUB_REPOSITORY_URL_MAX_LENGTH 255
/** The most repositories there can be, including the official one */
#define APP_HUB_REPOSITORY_MAX_COUNT 8

/**
 * A repository is a catalog of apps on a server. The catalog is the apps of the enabled repositories.
 * When more than one has the same app, the app of the repository that is first in the list is used.
 * The official repository is always first.
 */
struct AppHubRepository {
    /** The base URL: the address of the folder that has a folder per Tactility version, without a trailing slash */
    char url[APP_HUB_REPOSITORY_URL_MAX_LENGTH + 1];
    bool enabled;
    /** The repository of the Tactility project. It can be disabled, but not removed. */
    bool official;
};

/**
 * @param[in] repository the repository, only valid during the call
 * @return true to continue, false to stop
 */
typedef bool (*AppHubRepositoryVisitorFn)(const struct AppHubRepository* repository, void* context);

/**
 * Visit the repositories in order. The official one is first.
 * Reads storage, so it must not be called on the LVGL thread.
 */
error_t app_hub_repository_for_each(AppHubRepositoryVisitorFn visitor, void* context);

/**
 * Add a repository to the end of the list, enabled.
 * A change of the repositories discards the catalog and queues a refresh.
 * Reads and writes storage, so it must not be called on the LVGL thread.
 * @param[in] url the base URL, which must start with https://. A trailing slash is removed.
 * @retval ERROR_INVALID_ARGUMENT @a url is not a valid HTTPS URL
 * @retval ERROR_ALREADY_EXISTS the repository is in the list already
 * @retval ERROR_OUT_OF_RANGE the list has APP_HUB_REPOSITORY_MAX_COUNT repositories
 */
error_t app_hub_repository_add(const char* url);

/**
 * Remove a repository. A change of the repositories discards the catalog and queues a refresh.
 * Reads and writes storage, so it must not be called on the LVGL thread.
 * @retval ERROR_NOT_FOUND the repository isn't in the list
 * @retval ERROR_NOT_ALLOWED the repository is the official one
 */
error_t app_hub_repository_remove(const char* url);

/**
 * Enable or disable a repository, which includes the official one.
 * A change of the repositories discards the catalog and queues a refresh.
 * Reads and writes storage, so it must not be called on the LVGL thread.
 * @retval ERROR_NOT_FOUND the repository isn't in the list
 */
error_t app_hub_repository_set_enabled(const char* url, bool enabled);

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
