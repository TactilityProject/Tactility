#include <tactility/drivers/file_system.h>

#define INTERNAL_API(driver) ((struct FileSystem*)(driver)->api)

error_t file_system_mount(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return INTERNAL_API(driver)->mount(device);
}

error_t file_system_unmount(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return INTERNAL_API(driver)->unmount(device);
}

bool file_system_is_mounted(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return INTERNAL_API(driver)->is_mounted(device);
}

error_t file_system_get_mount_path(struct Device*, char* out_path) {
    const auto* driver = device_get_driver(device);
    return INTERNAL_API(driver)->get_mount_path(device, out_path);
}

const struct DeviceType FILE_SYSTEM_TYPE {
    .name = "file-system"
};
