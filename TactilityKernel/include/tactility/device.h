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
    /** Device address. Can represent an index, a memory address, or some kind of offset */
    int32_t address;

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
 * @retval ERROR_INVALID_STATE if the device is started, added, or has children
 * @retval ERROR_RESOURCE_BUSY if the device has outstanding device_get() references
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
 * @warning the driver's start_device callback must not call device_lock()/device_get()/
 *          device_start()/device_stop() on this same device - device_start() holds this
 *          device's lock for its entire body, so doing so would deadlock.
 * @param[in,out] device non-null device pointer
 * @retval ERROR_INVALID_STATE if the device is already started or not added
 * @retval ERROR_RESOURCE when driver binding fails
 * @retval ERROR_NONE on success
 */
error_t device_start(struct Device* device);

/**
 * Detach the driver.
 *
 * @warning the driver's stop_device callback must not call device_lock()/device_get()/
 *          device_start()/device_stop() on this same device - device_stop() holds this
 *          device's lock for its entire body, so doing so would deadlock.
 * @param[in,out] device non-null device pointer
 * @retval ERROR_INVALID_STATE if the device is not started
 * @retval ERROR_RESOURCE_BUSY if the device has outstanding device_get() references
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
 * Indicates whether device_construct() has been called and device_destruct() has not
 * subsequently been called (or has been followed by another device_construct()).
 *
 * @param[in] device non-null device pointer
 * @return true if the device is currently constructed
 */
bool device_is_constructed(const struct Device* device);

/**
 * Take a reference on a device, guaranteeing its driver data remains valid until a matching
 * device_put(). Named after the Linux kernel's get_device()/put_device(). Intended to bracket
 * a single hardware operation (one API call or a short sequence of them), not to be held across
 * a blocking wait or unrelated later work - device_stop() fails with ERROR_RESOURCE_BUSY while
 * any reference is outstanding.
 *
 * @param[in,out] device non-null device pointer, already known to be valid (e.g. a static
 *                devicetree pointer, or one captured earlier under a lock) - device_get() does
 *                not itself protect against a dangling/freed Device*. For a fresh lookup that
 *                might race a concurrent teardown of a dynamically-allocated Device, use one of
 *                the device_get_*() lookup functions below instead.
 * @retval ERROR_INVALID_STATE if the device is not started (or has been destructed)
 * @retval ERROR_NONE on success; caller must call device_put(device) exactly once
 */
error_t device_get(struct Device* device);

/**
 * Release a reference previously taken with a successful device_get() (or a successful
 * device_get_by_name()/device_get_first_by_type()/device_get_first_active_by_type()/
 * device_get_first_by_compatible()).
 *
 * @param[in,out] device non-null device pointer previously passed to a successful device_get() call
 */
void device_put(struct Device* device);

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
 * @brief Get the number of child devices.
 * @param[in] device non-null device pointer
 * @return the number of children
 */
size_t device_get_child_count(struct Device* device);

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
bool device_exists_of_type(const struct DeviceType* type) ;

/**
 * Find a device by its name.
 *
 * @param[in] name non-null device name to look up
 * @return the device pointer if found, or NULL if not found
 */
struct Device* device_find_by_name(const char* name) __attribute__((deprecated("Use device_get_by_name() and device_put()")));

/**
 * Find the first started device of the given type.
 *
 * @param[in] type non-null device type pointer
 * @return the first started device of the given type, or NULL if none found
 */
struct Device* device_find_first_active_by_type(const struct DeviceType* type) __attribute__((deprecated("Use device_get_first_active_by_type() and device_put()")));

/**
 * Find the first device of the given type.
 *
 * @param[in] type non-null device type pointer
 * @return the first device of the given type, or NULL if none found
 */
struct Device* device_find_first_by_type(const struct DeviceType* type) __attribute__((deprecated("Use device_get_first_by_type() and device_put()")));

/**
 * Find the first device whose driver matches the given compatible string.
 *
 * @param[in] compatible non-null compatible string to match
 * @return the first matching device, or NULL if none found
 */
struct Device* device_find_first_by_compatible(const char* compatible) __attribute__((deprecated("Use device_get_first_by_compatible() and device_put()")));

/**
 * Find a device by name and atomically take a reference on it (equivalent to a device_find_by_name()
 * immediately followed by a successful device_get(), but race-free: the lookup and the reference
 * are taken under the same lock, so a device that gets torn down concurrently either isn't found
 * or is safely referenced - there is no gap where a caller could be handed a pointer that's about
 * to become invalid). Prefer this over device_find_by_name() + device_get() for any device that
 * might be dynamically constructed/destructed at runtime (e.g. a hot-pluggable child device),
 * rather than a static devicetree-defined one.
 *
 * @param[in] name non-null device name to look up
 * @param[out] out_device receives the found device on success; untouched on failure
 * @retval ERROR_NOT_FOUND if no device with that name exists
 * @retval ERROR_INVALID_STATE if found but not started (no reference taken)
 * @retval ERROR_NONE on success; caller must call device_put(*out_device) exactly once
 */
error_t device_get_by_name(const char* name, struct Device** out_device);

/**
 * Find the first device of the given type and atomically take a reference on it. See
 * device_get_by_name() for why this is preferred over device_find_first_by_type() + device_get()
 * for dynamically constructed/destructed devices.
 *
 * @param[in] type non-null device type pointer
 * @param[out] out_device receives the found device on success; untouched on failure
 * @retval ERROR_NOT_FOUND if no device of that type exists
 * @retval ERROR_INVALID_STATE if found but not started (no reference taken)
 * @retval ERROR_NONE on success; caller must call device_put(*out_device) exactly once
 */
error_t device_get_first_by_type(const struct DeviceType* type, struct Device** out_device);

/**
 * Find the first started device of the given type and atomically take a reference on it. See
 * device_get_by_name() for why this is preferred over device_find_first_active_by_type() +
 * device_get() for dynamically constructed/destructed devices.
 *
 * @param[in] type non-null device type pointer
 * @param[out] out_device receives the found device on success; untouched on failure
 * @retval ERROR_NOT_FOUND if no started device of that type exists
 * @retval ERROR_NONE on success; caller must call device_put(*out_device) exactly once
 */
error_t device_get_first_active_by_type(const struct DeviceType* type, struct Device** out_device);

/**
 * Check if there is an active device of the provided type.
 *
 * @param[in] type non-null device type pointer
 * @retval ERROR_NOT_FOUND if no started device of that type exists
 * @retval ERROR_NONE on success; caller must call device_put(*out_device) exactly once
 */
bool device_has_active_by_type(const struct DeviceType* type);

/**
 * Find the first device whose driver matches the given compatible string and atomically take a
 * reference on it. See device_get_by_name() for why this is preferred over
 * device_find_first_by_compatible() + device_get() for dynamically constructed/destructed devices.
 *
 * @param[in] compatible non-null compatible string to match
 * @param[out] out_device receives the found device on success; untouched on failure
 * @retval ERROR_NOT_FOUND if no matching device exists
 * @retval ERROR_INVALID_STATE if found but not started (no reference taken)
 * @retval ERROR_NONE on success; caller must call device_put(*out_device) exactly once
 */
error_t device_get_first_by_compatible(const char* compatible, struct Device** out_device);

#ifdef __cplusplus
}
#endif
