// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include <tactility/concurrent/mutex.h>

struct Driver;

/** Enables discovering devices of the same type */
struct DeviceType {
    /* Placeholder because empty structs have a different size with C vs C++ compilers */
    uint8_t _;
};

/** Represents a piece of hardware */
struct Device {
    /** The name of the device. Valid characters: a-z a-Z 0-9 - _ . */
    const char* name;
    /** The configuration data for the device's driver */
    const void* config;
    /** The parent device that this device belongs to. Can be NULL, but only the root device should have a NULL parent. */
    struct Device* parent;
    /** Internal data */
    struct {
        /** Address of the API exposed by the device instance. */
        struct Driver* driver;
        /** The driver data for this device (e.g. a mutex) */
        void* driver_data;
        /** The mutex for device operations */
        struct Mutex mutex;
        /** The device state */
        struct {
            int start_result;
            bool started : 1;
            bool added : 1;
        } state;
        /** Private data */
        void* data;
    } internal;
};

/**
 * Holds a device pointer and a compatible string.
 * The device must not be constructed, added or started yet.
 * This is mused by the devicetree code generator and the application init sequence.
 */
struct CompatibleDevice {
    struct Device* device;
    const char* compatible;
};

/**
 * Initialize the properties of a device.
 *
 * @param[in] device a device with all non-internal properties set
 * @retval ERROR_OUT_OF_MEMORY
 * @retval ERROR_NONE
 */
error_t device_construct(struct Device* device);

/**
 * Deinitialize the properties of a device.
 * This fails when a device is busy or has children.
 *
 * @param[in] device
 * @retval ERROR_INVALID_STATE
 * @retval ERROR_NONE
 */
error_t device_destruct(struct Device* device);

/**
 * Indicates whether the device is in a state where its API is available
 *
 * @param[in] device non-null device pointer
 * @return true if the device is ready for use
 */
static inline bool device_is_ready(const struct Device* device) {
    return device->internal.state.started;
}

/**
 * Register a device to all relevant systems:
 * - the global ledger
 * - its parent (if any)
 * - a bus (if any)
 *
 * @param[in] device non-null device pointer
 * @retval ERROR_INVALID_STATE
 * @retval ERROR_NONE
 */
error_t device_add(struct Device* device);

/**
 * Deregister a device. Remove it from all relevant systems:
 * - the global ledger
 * - its parent (if any)
 * - a bus (if any)
 *
 * @param[in] device non-null device pointer
 * @retval ERROR_INVALID_STATE
 * @retval ERROR_NOT_FOUND
 * @retval ERROR_NONE
 */
error_t device_remove(struct Device* device);

/**
 * Attach the driver.
 *
 * @warning must call device_construct() and device_add() first
 * @param device
 * @retval ERROR_INVALID_STATE
 * @retval ERROR_RESOURCE when driver binding fails
 * @retval ERROR_NONE
 */
error_t device_start(struct Device* device);

/**
 * Detach the driver.
 *
 * @param device
 * @retval ERROR_INVALID_STATE
 * @retval ERROR_RESOURCE when driver unbinding fails
 * @retval ERROR_NONE
 */
error_t device_stop(struct Device* device);

/**
 * Set or unset a parent.
 * @warning must call before device_add()
 * @param device non-NULL device
 * @param parent nullable parent device
 */
void device_set_parent(struct Device* device, struct Device* parent);

error_t device_construct_add(struct Device* device, const char* compatible);

error_t device_construct_add_start(struct Device* device, const char* compatible);

static inline void device_set_driver(struct Device* device, struct Driver* driver) {
    device->internal.driver = driver;
}

static inline struct Driver* device_get_driver(struct Device* device) {
    return device->internal.driver;
}

static inline void device_set_driver_data(struct Device* device, void* driver_data) {
    device->internal.driver_data = driver_data;
}

static inline void* device_get_driver_data(struct Device* device) {
    return device->internal.driver_data;
}

static inline bool device_is_added(const struct Device* device) {
    return device->internal.state.added;
}

static inline void device_lock(struct Device* device) {
    mutex_lock(&device->internal.mutex);
}

static inline bool device_try_lock(struct Device* device) {
    return mutex_try_lock(&device->internal.mutex);
}

static inline void device_unlock(struct Device* device) {
    mutex_unlock(&device->internal.mutex);
}

static inline const struct DeviceType* device_get_type(struct Device* device) {
    return device->internal.driver ? device->internal.driver->deviceType : NULL;
}
/**
 * Iterate through all the known devices
 * @param callback_context the parameter to pass to the callback. NULL is valid.
 * @param on_device the function to call for each filtered device. return true to continue iterating or false to stop.
 */
void for_each_device(void* callback_context, bool(*on_device)(struct Device* device, void* context));

/**
 * Iterate through all the child devices of the specified device
 * @param callbackContext the parameter to pass to the callback. NULL is valid.
 * @param on_device the function to call for each filtered device. return true to continue iterating or false to stop.
 */
void for_each_device_child(struct Device* device, void* callbackContext, bool(*on_device)(struct Device* device, void* context));

/**
 * Iterate through all the known devices of a specific type
 * @param type the type to filter
 * @param callbackContext the parameter to pass to the callback. NULL is valid.
 * @param on_device the function to call for each filtered device. return true to continue iterating or false to stop.
 */
void for_each_device_of_type(const struct DeviceType* type, void* callbackContext, bool(*on_device)(struct Device* device, void* context));

#ifdef __cplusplus
}
#endif
