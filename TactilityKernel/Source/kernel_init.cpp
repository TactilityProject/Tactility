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

error_t kernel_init(struct Module* platform_module, struct Module* device_module, struct CompatibleDevice devicetree_devices[]) {
    LOG_I(TAG, "init");

    if (module_construct_add_start(&root_module) != ERROR_NONE) {
        LOG_E(TAG, "root module init failed");
        return ERROR_RESOURCE;
    }

    if (module_construct_add_start(platform_module) != ERROR_NONE) {
        LOG_E(TAG, "platform module init failed");
        return ERROR_RESOURCE;
    }

    if (device_module != nullptr) {
        if (module_construct_add_start(device_module) != ERROR_NONE) {
            LOG_E(TAG, "device module init failed");
            return ERROR_RESOURCE;
        }
    }

    if (devicetree_devices) {
        CompatibleDevice* compatible_device = devicetree_devices;
        while (compatible_device->device != nullptr) {
            if (device_construct_add_start(compatible_device->device, compatible_device->compatible) != ERROR_NONE) {
                LOG_E(TAG, "kernel_init failed to construct device: %s (%s)", compatible_device->device->name, compatible_device->compatible);
                return ERROR_RESOURCE;
            }
            compatible_device++;
        }
    }

    LOG_I(TAG, "init done");
    return ERROR_NONE;
}

#ifdef __cplusplus
}
#endif
