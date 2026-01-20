#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <Tactility/Bus.h>
#include <Tactility/concurrent/Mutex.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Driver;

/** Represents a piece of hardware */
struct Device {
    /** The name of the device. Valid characters: a-z a-Z 0-9 - _ . */
    const char* name;
    /** The configuration data for the device's driver */
    void* config;
    /** Internal data */
    struct {
        /** The parent device that this device belongs to. Can be NULL, but only the root device should have a NULL parent. */
        struct Device* parent;
        /** Address of the API exposed by the device instance. */
        struct Driver* driver;
        /** The driver data for this device (e.g. a mutex) */
        void* driver_data;
        /** The bus that is owned by this device. This bus can contain child devices to make them discoverable. Can be NULL. */
        struct Bus* bus;
        /** The mutex for device operations */
        struct Mutex mutex;
        /** The device state */
        struct {
            uint8_t start_result;
            bool started : 1;
            bool added : 1;
        } state;
        /** Private data */
        void* data;
    } internal;
};

/**
 * Initialize the properties of a device.
 *
 * @param[in] dev a device with all non-internal properties set
 */
void device_construct(struct Device* device);

/**
 * Deinitialize the properties of a device.
 * This fails when a device is busy or has children.
 *
 * @param[in] dev
 * @return the result code (0 for success)
 */
int device_destruct(struct Device* device);

/**
 * Indicates whether the device is in a state where its API is available
 *
 * @param[in] dev non-null device pointer
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
 */
void device_add(struct Device* device);

/**
 * Deregister a device. Remove it from all relevant systems:
 * - the global ledger
 * - its parent (if any)
 * - a bus (if any)
 *
 * @param[in] device non-null device pointer
 * @return true when the device was found and deregistered
 */
bool device_remove(struct Device* device);

/**
 * Attach the driver.
 *
 * @warning must call device_construct() and device_add() first
 * @param device
 * @return ERROR_INVALID_STATE or otherwise the value of the driver binding result (0 on success)
 */
int device_start(struct Device* device);

/**
 * Detach the driver.
 *
 * @param device
 * @return ERROR_INVALID_STATE or otherwise the value of the driver unbinding result (0 on success)
 */
int device_stop(struct Device* device);

/**
 * Set or unset a parent.
 * @warning must call before device_add()
 * @param device non-NULL device
 * @param parent nullable parent device
 */
void device_set_parent(struct Device* device, struct Device* parent);

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

static inline struct Bus* device_get_bus(const struct Device* device) {
    return device->internal.bus;
}

static inline void device_set_bus(struct Device* device, struct Bus* bus) {
    device->internal.bus = bus;
}

static inline const char* device_get_bus_name(const struct Device* device) {
    return device->internal.bus ? device->internal.bus->name : "";
}

static inline void device_lock(struct Device* device) {
    mutex_lock(&device->internal.mutex);
}

static inline int device_try_lock(struct Device* device) {
    return mutex_try_lock(&device->internal.mutex);
}

static inline void device_unlock(struct Device* device) {
    mutex_unlock(&device->internal.mutex);
}

#ifdef __cplusplus
}
#endif
