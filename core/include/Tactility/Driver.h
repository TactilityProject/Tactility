#pragma once

#include <Tactility/Bus.h>
#include <Tactility/Device.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Driver {
    /** The driver name */
    const char* name;
    /** Array of const char*, terminated by NULL */
    const char** compatible;
    /** Function to initialize the driver for a device */
    int (*start_device)(struct Device* dev);
    /** Function to deinitialize the driver for a device */
    int (*stop_device)(struct Device* dev);
    /** Contains the driver's functions */
    const void* api;
    /** Internal data */
    struct {
        /** Contains private data */
        void* data;
    } internal;
};

int driver_construct(struct Driver* drv);

int driver_destruct(struct Driver* drv);

struct Driver* driver_find(const char* compatible);

int driver_bind(struct Driver* drv, struct Device* dev);

int driver_unbind(struct Driver* drv, struct Device* dev);

#ifdef __cplusplus
}
#endif
