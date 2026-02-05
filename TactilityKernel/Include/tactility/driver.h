// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "error.h"
#include <stdbool.h>

struct Device;
struct DeviceType;
struct Module;
struct DriverInternal;

struct Driver {
    /** The driver name */
    const char* name;
    /** Array of const char*, terminated by NULL */
    const char**compatible;
    /** Function to initialize the driver for a device */
    error_t (*start_device)(struct Device* dev);
    /** Function to deinitialize the driver for a device */
    error_t (*stop_device)(struct Device* dev);
    /** Contains the driver's functions */
    const void* api;
    /** Which type of devices this driver creates (can be NULL) */
    const struct DeviceType* device_type;
    /** The module that owns this driver. When it is NULL, the system owns the driver and it cannot be removed from registration. */
    const struct Module* owner;
    /** Internal data */
    struct DriverInternal* internal;
};

/**
 * @brief Construct a driver.
 *
 * This initializes the internal state of the driver.
 *
 * @param driver The driver to construct.
 * @return ERROR_NONE if successful, or an error code otherwise.
 */
error_t driver_construct(struct Driver* driver);

/**
 * @brief Destruct a driver.
 *
 * This cleans up the internal state of the driver.
 *
 * @param driver The driver to destruct.
 * @return ERROR_NONE if successful, or an error code otherwise.
 */
error_t driver_destruct(struct Driver* driver);

/**
 * @brief Add a driver to the system.
 *
 * This registers the driver so it can be used to bind to devices.
 *
 * @param driver The driver to add.
 * @return ERROR_NONE if successful, or an error code otherwise.
 */
error_t driver_add(struct Driver* driver);

/**
 * @brief Remove a driver from the system.
 *
 * This unregisters the driver.
 *
 * @param driver The driver to remove.
 * @return ERROR_NONE if successful, or an error code otherwise.
 */
error_t driver_remove(struct Driver* driver);

/**
 * @brief Construct and add a driver to the system.
 *
 * @param driver The driver to construct and add.
 * @return ERROR_NONE if successful, or an error code otherwise.
 */
error_t driver_construct_add(struct Driver* driver);

/**
 * @brief Remove and destruct a driver.
 *
 * @param driver The driver to remove and destruct.
 * @return ERROR_NONE if successful, or an error code otherwise.
 */
error_t driver_remove_destruct(struct Driver* driver);

/**
 * @brief Bind a driver to a device.
 *
 * This calls the driver's start_device function and increments the driver's use count.
 *
 * @param driver The driver to bind.
 * @param device The device to bind to.
 * @return ERROR_NONE if successful, or an error code otherwise.
 */
error_t driver_bind(struct Driver* driver, struct Device* device);

/**
 * @brief Unbind a driver from a device.
 *
 * This calls the driver's stop_device function and decrements the driver's use count.
 *
 * @param driver The driver to unbind.
 * @param device The device to unbind from.
 * @return ERROR_NONE if successful, or an error code otherwise.
 */
error_t driver_unbind(struct Driver* driver, struct Device* device);

/**
 * @brief Check if a driver is compatible with a given string.
 *
 * @param driver The driver to check.
 * @param compatible The compatibility string to check.
 * @return true if compatible, false otherwise.
 */
bool driver_is_compatible(struct Driver* driver, const char* compatible);

/**
 * @brief Find a driver compatible with a given string.
 *
 * @param compatible The compatibility string to find.
 * @return The compatible driver, or NULL if not found.
 */
struct Driver* driver_find_compatible(const char* compatible);

/**
 * @brief Get the device type of a driver.
 *
 * @param driver The driver to get the device type from.
 * @return The device type of the driver.
 */
const struct DeviceType* driver_get_device_type(struct Driver* driver);

#ifdef __cplusplus
}
#endif
