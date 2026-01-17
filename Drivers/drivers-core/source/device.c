#include "device.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_DEVICE_INDEX_SIZE 64

// TODO: Automatically increase allocated size
static const struct device* device_index[128] = { 0 };

uint8_t device_init(struct device* const dev) {
    assert(dev != nullptr);
    printf("device_init: %s\n", dev->name);

    if (!dev->state.initialized) {
        if (dev->operations.init == nullptr) {
            dev->state.initialized = true;
            dev->state.init_result = 0U;
        } else {
            dev->state.init_result = dev->operations.init(dev);
            dev->state.initialized = dev->state.init_result == 0U;
        }
    }

    return dev->state.init_result;
}

bool device_init_all(struct device** const device_array) {
    assert(device_array != nullptr);
    struct device** current_device = device_array;
    bool all_succeeded = true;
    while (*current_device != nullptr) {
        struct device* device = *current_device;
        if (device_init(device) != 0U) {
            all_succeeded = false;
        }
        current_device++;
    }
    return all_succeeded;
}

uint8_t device_deinit(struct device* const dev) {
    assert(dev != nullptr);

    if (dev->state.initialized) {
        if (dev->operations.deinit != nullptr) {
            dev->state.init_result = dev->operations.deinit(dev);
            if (dev->state.init_result == 0U) {
                dev->state.initialized = false;
            }
        } else {
            dev->state.initialized = false;
        }
    }

   return !dev->state.init_result;
}

bool device_is_ready(const struct device* const dev) {
    assert(dev != nullptr);
    return dev->state.initialized && (dev->state.init_result == 0U);
}


bool device_add(const struct device* dev) {
    assert(dev != nullptr);
    for (int i = 0; i < CONFIG_DEVICE_INDEX_SIZE; i++) {
        if (device_index[i] == nullptr) {
            device_index[i] = dev;
            return true;
        }
    }
    return false;
}

void device_add_all(struct device** const device_array) {
    assert(device_array != nullptr);
    struct device** current_device = device_array;
    while (*current_device != nullptr) {
        struct device* device = *current_device;
        device_add(device);
        current_device++;
    }
}

bool device_remove(const struct device* dev) {
    for (int i = 0; i < CONFIG_DEVICE_INDEX_SIZE; i++) {
        if (device_index[i] == dev) {
            device_index[i] = nullptr;
            return true;
        }
    }
    return false;
}

bool device_find_next(const char* label, const struct device** device) {
    bool found_first = (device == nullptr);
    for (int device_idx = 0; device_idx < CONFIG_DEVICE_INDEX_SIZE; device_idx++) {
        auto indexed_device = device_index[device_idx];
        if (indexed_device != nullptr) {
            if (!found_first) {
                if (indexed_device == *device) {
                    found_first = true;
                }
            } else {
                for (int label_idx = 0; label_idx< indexed_device->metadata.num_node_labels; ++label_idx) {
                    const char* indexed_device_label = indexed_device->metadata.node_labels[label_idx];
                    if (strcmp(indexed_device_label, label) == 0) {
                        *device = indexed_device;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

