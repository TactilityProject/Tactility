#include <tactility/kernel_init.h>
#include <tactility/log.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAG LOG_TAG(kernel)

struct ModuleParent kernel_module_parent = {
    "kernel",
    nullptr
};

static error_t init_kernel_drivers() {
    extern Driver root_driver;
    if (driver_construct(&root_driver) != ERROR_NONE) return ERROR_RESOURCE;
    return ERROR_NONE;
}

error_t kernel_init(struct Module* platform_module, struct Module* device_module, struct CompatibleDevice devicetree_devices[]) {
    LOG_I(TAG, "init");

    if (module_parent_construct(&kernel_module_parent) != ERROR_NONE) {
        LOG_E(TAG, "init failed to create kernel module parent");
        return ERROR_RESOURCE;
    }

    if (init_kernel_drivers() != ERROR_NONE) {
        LOG_E(TAG, "init failed to init kernel drivers");
        return ERROR_RESOURCE;
    }

    module_set_parent(platform_module, &kernel_module_parent);
    if (module_start(platform_module) != ERROR_NONE) {
        LOG_E(TAG, "init failed to start platform module");
        return ERROR_RESOURCE;
    }

    module_set_parent(device_module, &kernel_module_parent);
    if (module_start(device_module) != ERROR_NONE) {
        LOG_E(TAG, "init failed to start device module");
        return ERROR_RESOURCE;
    }

    CompatibleDevice* compatible_device = devicetree_devices;
    while (compatible_device->device != nullptr) {
        if (device_construct_add_start(compatible_device->device, compatible_device->compatible) != ERROR_NONE) {
            LOG_E(TAG, "kernel_init failed to construct device: %s (%s)", compatible_device->device->name, compatible_device->compatible);
            return ERROR_RESOURCE;
        }
        compatible_device++;
    }

    LOG_I(TAG, "init done");
    return ERROR_NONE;
};

#ifdef __cplusplus
}
#endif
