#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

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

#define DEVICE_LABEL(key, value) #key":"#value
#define DEVICE_LABEL_FOR_TYPE(_type) DEVICE_LABEL(type, _type)

struct device_metadata {
    /* @brief number of elements in the nodelabels array */
    size_t num_node_labels;
    /* @brief array of node labels as strings, exactly as they
     *        appear in the final devicetree
     */
    const char** node_labels;
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

uint8_t device_init(struct device* dev);

bool device_init_all(struct device** device_array);

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
bool device_add(const struct device* dev);

/**
 * Register all devices in the specified array.
 * The array must be null-terminated.
 *
 * @param[in] device_array non-null array of devices
 */
void device_add_all(struct device** device_array);

bool device_remove(const struct device* dev);

bool device_find_next(const char* label, const struct device** device);

#ifdef __cplusplus
}
#endif
