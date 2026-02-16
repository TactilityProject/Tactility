#include "tactility/dts.h"
#include <tactility/kernel_init.h>
#include <tactility/log.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAG "kernel"

extern const struct ModuleSymbol KERNEL_SYMBOLS[];

static error_t start() {
    extern Driver root_driver;
    if (driver_construct_add(&root_driver) != ERROR_NONE) return ERROR_RESOURCE;
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module root_module = {
    .name = "kernel",
    .start = start,
    .stop = stop,
    .symbols = (const struct ModuleSymbol*)KERNEL_SYMBOLS,
    .internal = nullptr
};

error_t kernel_init(Module* device_module, Module* dts_modules[], DtsDevice dts_devices[]) {
    LOG_I(TAG, "init");

    if (module_construct_add_start(&root_module) != ERROR_NONE) {
        LOG_E(TAG, "root module init failed");
        return ERROR_RESOURCE;
    }

    Module** dts_module = dts_modules;
    while (*dts_module != nullptr) {
        if (module_construct_add_start(*dts_module) != ERROR_NONE) {
            LOG_E(TAG, "dts module init failed: %s", (*dts_module)->name);
            return ERROR_RESOURCE;
        }
        dts_module++;
    }

    if (device_module != nullptr) {
        if (module_construct_add_start(device_module) != ERROR_NONE) {
            LOG_E(TAG, "device module init failed");
            return ERROR_RESOURCE;
        }
    }

    if (dts_devices) {
        DtsDevice* dts_device = dts_devices;
        while (dts_device->device != nullptr) {
            if (dts_device->status == DTS_DEVICE_STATUS_OKAY) {
                if (device_construct_add_start(dts_device->device, dts_device->compatible) != ERROR_NONE) {
                    LOG_E(TAG, "kernel_init failed to construct+add+start device: %s (%s)", dts_device->device->name, dts_device->compatible);
                    return ERROR_RESOURCE;
                }
            } else if (dts_device->status == DTS_DEVICE_STATUS_DISABLED) {
                if (device_construct_add(dts_device->device, dts_device->compatible) != ERROR_NONE) {
                    LOG_E(TAG, "kernel_init failed to construct+add device: %s (%s)", dts_device->device->name, dts_device->compatible);
                    return ERROR_RESOURCE;
                }
            } else {
                check(false, "DTS status not implemented");
            }
            dts_device++;
        }
    }

    LOG_I(TAG, "init done");
    return ERROR_NONE;
}

#ifdef __cplusplus
}
#endif
