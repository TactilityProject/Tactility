#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

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
    /** Which type of devices this driver creates (can be NULL) */
    const struct DeviceType* device_type;
    /** Internal data */
    struct {
        /** Contains private data */
        void* data;
    } internal;
};

int driver_construct(struct Driver* driver);

int driver_destruct(struct Driver* driver);

struct Driver* driver_find(const char* compatible);

int driver_bind(struct Driver* driver, struct Device* device);

int driver_unbind(struct Driver* driver, struct Device* device);

static inline const struct DeviceType* driver_get_device_type(struct Driver* driver) {
    return driver->device_type;
}

#ifdef __cplusplus
}
#endif
