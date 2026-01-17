#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct device;

struct device_operations {
    /** Initialization function */
    int (*init)(const struct device* device);
    /** De-initialization function */
    int (*deinit)(const struct device* device);
};

struct device_state {
    uint8_t init_result;
    bool initialized : 1;
};

struct device_metadata {
    /** @brief number of elements in the compatible array */
    size_t compatible_count;
    /** @brief array of strings containing the compatible device names */
    const char** compatible;
};

struct device {
	/** Name of the device as defined in the dts file */
    const char* name;
	/** Address of device instance configuration. This relates to the parameters that are specified in the dts file*/
    const void* config;
	/** Address of the API exposed by the device instance */
    const void* api;
	/** The device state */
    struct device_state state;
	/** Address of the device's private data */
    void* data;
	/** Device operations: used for initializing and deinitializing */
    struct device_operations operations;
    /** Device metadata */
    struct device_metadata metadata;
};

/**
 * Initialize a device.
 * @param[in] dev
 * @return the return code of the device's init function
 */
uint8_t device_init(struct device* dev);

/**
 * Initialize an array of devices.
 * @param[in] device_array a null-terminated array of devices
 * @retval true if all devices initialized successfully
 * @retval false if any device failed to initialize
 */
bool device_init_all(struct device** device_array);

/**
 * Deinitialize a device.
 * @param[in] dev
 * @return the return code of the device's deinit function
 */
uint8_t device_deinit(struct device* dev);

/**
 * Indicated whether the device is in a state where its API is available
 *
 * @param[in] dev non-null device pointer
 * @return true if the device is ready for use
 */
bool device_is_ready(const struct device* dev);

/**
 * Register a single device.
 *
 * @param[in] dev non-null device pointer
 */
void device_add(const struct device* dev);

/**
 * Register all devices in the specified array.
 * The array must be null-terminated.
 *
 * @param[in] device_array non-null array of devices
 */
void device_add_all(struct device** device_array);

/**
 * Deregister a device
 * @param[in] dev non-null device pointer
 * @return true when the device was found and deregistered
 */
bool device_remove(const struct device* dev);

/**
 * Iterate the devicetree. Find the next with the specified label.
 * @param[in] identifier the identifier of the device, such as "root" or "i2c-controller"
 * @param[inout] dev a pointer to a device pointer in the tree, or nullptr to start searching for the first device
 * @return true if a device was found
 */
bool device_find_next_by_compatible(const char* identifier, const struct device** dev);

#ifdef __cplusplus
}
#endif
