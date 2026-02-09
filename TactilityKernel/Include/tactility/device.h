// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "driver.h"
#include "error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/freertos/freertos.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Driver;
struct DeviceInternal;

/** Enables discovering devices of the same type */
struct DeviceType {
    const char* name;
};

/** Represents a piece of hardware */
struct Device {
    /** The name of the device. Valid characters: a-z a-Z 0-9 - _ . */
    const char* name;

    /** The configuration data for the device's driver */
    const void* config;

    /** The parent device that this device belongs to. Can be NULL, but only the root device should have a NULL parent. */
    struct Device* parent;

    /**
     * Internal state managed by the kernel.
     * Device implementers should initialize this to NULL.
     * Do not access or modify directly; use device_* functions.
     */
    struct DeviceInternal* internal;
};

/**
 * Holds a device pointer and a compatible string.
 * The device must not be constructed, added or started yet.
 * This is used by the devicetree code generator and the application init sequence.
 */
struct CompatibleDevice {
    struct Device* device;
    const char* compatible;
};

/**
 * Initialize the properties of a device.
 *
 * @param[in,out] device a device with all non-internal properties set
 * @retval ERROR_OUT_OF_MEMORY if internal data allocation failed
 * @retval ERROR_NONE on success
 */
error_t device_construct(struct Device* device);

/**
 * Deinitialize the properties of a device.
 * This fails when a device is busy or has children.
 *
 * @param[in,out] device non-null device pointer
 * @retval ERROR_INVALID_STATE if the device is busy or has children
 * @retval ERROR_NONE on success
 */
error_t device_destruct(struct Device* device);

/**
 * Register a device to all relevant systems:
 * - the global ledger
 * - its parent (if any)
 *
 * @param[in,out] device non-null device pointer
 * @retval ERROR_INVALID_STATE if the device is already added
 * @retval ERROR_NONE on success
 */
error_t device_add(struct Device* device);

/**
 * Deregister a device. Remove it from all relevant systems:
 * - the global ledger
 * - its parent (if any)
 *
 * @param[in,out] device non-null device pointer
 * @retval ERROR_INVALID_STATE if the device is still started
 * @retval ERROR_NOT_FOUND if the device was not found in the system
 * @retval ERROR_NONE on success
 */
error_t device_remove(struct Device* device);

/**
 * Attach the driver.
 *
 * @warning must call device_construct() and device_add() first
 * @param[in,out] device non-null device pointer
 * @retval ERROR_INVALID_STATE if the device is already started or not added
 * @retval ERROR_RESOURCE when driver binding fails
 * @retval ERROR_NONE on success
 */
error_t device_start(struct Device* device);

/**
 * Detach the driver.
 *
 * @param[in,out] device non-null device pointer
 * @retval ERROR_INVALID_STATE if the device is not started
 * @retval ERROR_RESOURCE when driver unbinding fails
 * @retval ERROR_NONE on success
 */
error_t device_stop(struct Device* device);

/**
 * Construct and add a device with the given compatible string.
 *
 * @param[in,out] device non-NULL device
 * @param[in] compatible compatible string
 * @retval ERROR_NONE on success
 * @retval error_t error code on failure
 */
error_t device_construct_add_start(struct Device* device, const char* compatible);

/**
 * Construct and add a device with the given compatible string.
 *
 * @param[in,out] device non-NULL device
 * @param[in] compatible compatible string
 * @retval ERROR_NONE on success
 * @retval error_t error code on failure
 */
error_t device_construct_add(struct Device* device, const char* compatible);

/**
 * Set or unset a parent.
 *
 * @warning must call before device_add()
 * @param[in,out] device non-NULL device
 * @param[in] parent nullable parent device
 */
void device_set_parent(struct Device* device, struct Device* parent);

/**
 * Set the driver for a device.
 *
 * @warning must call before device_add()
 * @param[in,out] device non-NULL device
 * @param[in] driver nullable driver
 */
void device_set_driver(struct Device* device, struct Driver* driver);

/**
 * Get the driver for a device.
 *
 * @param[in] device non-null device pointer
 * @return the driver, or NULL if the device has no driver
 */
struct Driver* device_get_driver(struct Device* device);

/**
 * Get the parent device of a device.
 *
 * @param[in] device non-null device pointer
 * @return the parent device, or NULL if the device has no parent
 */
struct Device* device_get_parent(struct Device* device);

/**
 * Indicates whether the device is in a state where its API is available
 *
 * @param[in] device non-null device pointer
 * @return true if the device is ready for use
 */
bool device_is_ready(const struct Device* device);

/**
 * Indicates whether the device is compatible with the given compatible string.
 * @param[in] device non-null device pointer
 * @param[in] compatible compatible string
 * @return true if the device is compatible
 */
bool device_is_compatible(const struct Device* device, const char* compatible);

/**
 * Set the driver data for a device.
 *
 * @param[in,out] device non-null device pointer
 * @param[in] driver_data the driver data
 */
void device_set_driver_data(struct Device* device, void* driver_data);

/**
 * Get the driver data for a device.
 *
 * @param[in] device non-null device pointer
 * @return the driver data
 */
void* device_get_driver_data(struct Device* device);

/**
 * Indicates whether the device has been added to the system.
 *
 * @param[in] device non-null device pointer
 * @return true if the device has been added
 */
bool device_is_added(const struct Device* device);

/**
 * Lock the device for exclusive access.
 *
 * @param[in,out] device non-null device pointer
 */
void device_lock(struct Device* device);

/**
 * Try to lock the device for exclusive access.
 *
 * @param[in,out] device non-null device pointer
 * @param[in] timeout how long to wait for the lock
 * @return true if the device was locked successfully
 */
bool device_try_lock(struct Device* device, TickType_t timeout);

/**
 * Unlock the device.
 *
 * @param[in,out] device non-null device pointer
 */
void device_unlock(struct Device* device);

/**
 * Get the type of a device.
 *
 * @param[in] device non-null device pointer
 * @return the device type
 */
const struct DeviceType* device_get_type(struct Device* device);

/**
 * Iterate through all the known devices
 *
 * @param[in] callback_context the parameter to pass to the callback. NULL is valid.
 * @param[in] on_device the function to call for each filtered device. return true to continue iterating or false to stop.
 */
void device_for_each(void* callback_context, bool(*on_device)(struct Device* device, void* context));

/**
 * Iterate through all the child devices of the specified device
 *
 * @param[in] device non-null device pointer
 * @param[in] callback_context the parameter to pass to the callback. NULL is valid.
 * @param[in] on_device the function to call for each filtered device. return true to continue iterating or false to stop.
 */
void device_for_each_child(struct Device* device, void* callback_context, bool(*on_device)(struct Device* device, void* context));

/**
 * Iterate through all the known devices of a specific type
 *
 * @param[in] type the type to filter
 * @param[in] callback_context the parameter to pass to the callback. NULL is valid.
 * @param[in] on_device the function to call for each filtered device. return true to continue iterating or false to stop.
 */
void device_for_each_of_type(const struct DeviceType* type, void* callback_context, bool(*on_device)(struct Device* device, void* context));

/**
 * Check if a device of the specified type exists.
 *
 * @param[in] type the type to check
 * @return true if a device of the specified type exists
 */
bool device_exists_of_type(const struct DeviceType* type);

/**
 * Find a device by its name.
 *
 * @param[in] name non-null device name to look up
 * @return the device pointer if found, or NULL if not found
 */
struct Device* device_find_by_name(const char* name);

#ifdef __cplusplus
}
#endif
