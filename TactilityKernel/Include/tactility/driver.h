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

struct Driver {
    /** The driver name */
    const char* name;
    /** Array of const char*, terminated by NULL */
    const char**compatible;
    /** Function to initialize the driver for a device */
    error_t (*startDevice)(struct Device* dev);
    /** Function to deinitialize the driver for a device */
    error_t (*stopDevice)(struct Device* dev);
    /** Contains the driver's functions */
    const void* api;
    /** Which type of devices this driver creates (can be NULL) */
    const struct DeviceType* deviceType;
    /** The module that owns this driver. When it is NULL, the system owns the driver and it cannot be removed from registration. */
    const struct Module* owner;
    /** Internal data */
    void* internal;
};

error_t driver_construct(struct Driver* driver);

error_t driver_destruct(struct Driver* driver);

error_t driver_bind(struct Driver* driver, struct Device* device);

error_t driver_unbind(struct Driver* driver, struct Device* device);

bool driver_is_compatible(struct Driver* driver, const char* compatible);

struct Driver* driver_find_compatible(const char* compatible);

static inline const struct DeviceType* driver_get_device_type(struct Driver* driver) {
    return driver->deviceType;
}

#ifdef __cplusplus
}
#endif
