#pragma once

#include <tt_bundle.h>

#include <stdio.h>
#include <stdbool.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* AppHandle;

/** Important: These values must map to tt::app::Result values exactly */
typedef enum {
    APP_RESULT_OK = 0,
    APP_RESULT_CANCELLED = 1,
    APP_RESULT_ERROR = 2
} AppResult;

typedef unsigned int AppLaunchId;

/** Important: These function types must map to t::app types exactly. All void* data is nullable. */
typedef void* (*AppCreateData)();
typedef void (*AppDestroyData)(void* data);
typedef void (*AppOnCreate)(AppHandle app, void* data);
typedef void (*AppOnDestroy)(AppHandle app, void* data);
typedef void (*AppOnShow)(AppHandle app, void* data, lv_obj_t* parent);
typedef void (*AppOnHide)(AppHandle app, void* data);
typedef void (*AppOnResult)(AppHandle app, void* data, AppLaunchId launchId, AppResult result, BundleHandle resultData);

/** All callback types are nullable */
typedef struct {
    /** The application can allocate data to re-use later (e.g. struct with state) */
    AppCreateData createData;
    /** If createData is specified, this one must be specified too */
    AppDestroyData destroyData;
    /** Called when the app is launched (started) */
    AppOnCreate onCreate;
    /** Called when the app is exited (stopped) */
    AppOnDestroy onDestroy;
    /** Called when the app is about to be shown to the user (app becomes visible) */
    AppOnShow onShow;
    /** Called when the app is about to be invisible to the user (e.g. other app was launched by this app, and this app goes to the background) */
    AppOnHide onHide;
    /** Called when the app receives a result after launching another app */
    AppOnResult onResult;
} AppRegistration;

/** This is used to register the manifest of an external app. */
void tt_app_register(const AppRegistration app);

/** @return the bundle that belongs to this application, or null if it wasn't started with parameters. */
BundleHandle tt_app_get_parameters(AppHandle handle);

/**
 * Set the result before closing an app.
 * The result and bundle are passed along to the app that launched this app, when this app is closed.
 * @param[in] handle the app handle to set the result for
 * @param[in] result the result state to set
 * @param[in] bundle the result bundle to set (can be null)
 */
void tt_app_set_result(AppHandle handle, AppResult result, BundleHandle bundle);

/** @return true if a result was set for this app context */
bool tt_app_has_result(AppHandle handle);

/** Get the path to the user data directory for this app.
 * The app can store user-specific (mutable) data in there such as app settings.
 * @param[in] handle the app handle
 * @param[out] buffer the output buffer (recommended size is 256 bytes)
 * @param[inout] size used as input for maximum buffer size (including null terminator) and is set with the path string length by this function
 */
void tt_app_get_user_data_path(AppHandle handle, char* buffer, size_t* size);

/** Resolve a child path in the user directory of this app.
 * The app can store user-specific (mutable) data in there such as app settings.
 * @param[in] handle the app handle
 * @param[in] childPath the child path to resolve
 * @param[out] buffer the output buffer (recommended size is 256 bytes)
 * @param[inout] size used as input for maximum buffer size (including null terminator) and is set with the path string length by this function
 */
void tt_app_get_user_data_child_path(AppHandle handle, const char* childPath, char* buffer, size_t* size);

/** Get the path to the assets directory of this app.
 * The content in this path should be treated as read-only.
 * @param[in] handle the app handle
 * @param[out] buffer the output buffer (recommended size is 256 bytes)
 * @param[inout] size used as input for maximum buffer size (including null terminator) and is set with the path string length by this function
 */
void tt_app_get_assets_path(AppHandle handle, char* buffer, size_t* size);

/** Resolve a child path in the assets directory of this app.
 * The content in this path should be treated as read-only.
 * @param[in] handle the app handle
 * @param[in] childPath the child path to resolve
 * @param[out] buffer the output buffer (recommended size is 256 bytes)
 * @param[inout] size used as input for maximum buffer size (including null terminator) and is set with the path string length by this function
 */
void tt_app_get_assets_child_path(AppHandle handle, const char* childPath, char* buffer, size_t* size);

/**
 * Start an app by id.
 * @param[in] appId the app manifest id
 */
void tt_app_start(const char* appId);

/** Stop the currently running app */
void tt_app_stop();

/**
 * Start an app by id and bundle.
 * @param[in] appId the app manifest id
 * @param[in] parameters the parameters to pass onto the starting app
 */
void tt_app_start_with_bundle(const char* appId, BundleHandle parameters);

#ifdef __cplusplus
}
#endif