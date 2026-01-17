#include <tactility/device.h>
#include <tactility/log.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_DEVICE_INDEX_SIZE 64
#define TAG "device"

// TODO: Automatically increase allocated size
static const struct device* device_index[CONFIG_DEVICE_INDEX_SIZE ] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

uint8_t device_init(struct device* const dev) {
    assert(dev != NULL);
    LOG_I(TAG, "init %s", dev->name);

    if (!dev->state.initialized) {
        if (dev->operations.init == NULL) {
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
    assert(device_array != NULL);
    struct device** current_device = device_array;
    bool all_succeeded = true;
    while (*current_device != NULL) {
        struct device* device = *current_device;
        if (device_init(device) != 0U) {
            all_succeeded = false;
        }
        current_device++;
    }
    return all_succeeded;
}

uint8_t device_deinit(struct device* const dev) {
    assert(dev != NULL);
    LOG_I(TAG, "deinit %s", dev->name);

    if (dev->state.initialized) {
        if (dev->operations.deinit != NULL) {
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
    assert(dev != NULL);
    return dev->state.initialized && (dev->state.init_result == 0U);
}


void device_add(const struct device* dev) {
    assert(dev != NULL);
    LOG_I(TAG, "add %s", dev->name);
    for (int i = 0; i < CONFIG_DEVICE_INDEX_SIZE; i++) {
        if (device_index[i] == NULL) {
            device_index[i] = dev;
            return;
        }
    }
    assert(false); // out of space
}

void device_add_all(struct device** const device_array) {
    assert(device_array != NULL);
    struct device** current_device = device_array;
    while (*current_device != NULL) {
        struct device* device = *current_device;
        device_add(device);
        current_device++;
    }
}

bool device_remove(const struct device* dev) {
    assert(dev != NULL);
    LOG_I(TAG, "remove %s", dev->name);
    for (int i = 0; i < CONFIG_DEVICE_INDEX_SIZE; i++) {
        if (device_index[i] == dev) {
            device_index[i] = NULL;
            return true;
        }
    }
    return false;
}

bool device_find_next_by_compatible(const char* identifier, const struct device** dev) {
    bool found_first = (*dev == NULL);
    for (int device_idx = 0; device_idx < CONFIG_DEVICE_INDEX_SIZE; device_idx++) {
        const struct device* indexed_device = device_index[device_idx];
        if (indexed_device != NULL) {
            if (!found_first) {
                if (indexed_device == *dev) {
                    found_first = true;
                }
            } else {
                for (int label_idx = 0; label_idx< indexed_device->metadata.compatible_count; ++label_idx) {
                    const char* indexed_device_label = indexed_device->metadata.compatible[label_idx];
                    if (strcmp(indexed_device_label, identifier) == 0) {
                        *dev = indexed_device;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}
