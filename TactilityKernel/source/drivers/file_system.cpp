#include <tactility/drivers/file_system.h>
#include <tactility/device.h>

#define INTERNAL_API(driver) ((FileSystemApi*)(driver)->api)

extern "C" {

error_t file_system_mount(Device* device, const char* mount_path) {
    const auto* driver = device_get_driver(device);
    return INTERNAL_API(driver)->mount(device, mount_path);
}

error_t file_system_unmount(Device* device) {
    const auto* driver = device_get_driver(device);
    return INTERNAL_API(driver)->unmount(device);
}

bool file_system_is_mounted(Device* device) {
    const auto* driver = device_get_driver(device);
    return INTERNAL_API(driver)->is_mounted(device);
}

error_t file_system_get_mount_path(Device* device, char* out_path, size_t out_path_size) {
    const auto* driver = device_get_driver(device);
    return INTERNAL_API(driver)->get_mount_path(device, out_path, out_path_size);
}

const DeviceType FILE_SYSTEM_TYPE {
    .name = "file-system"
};

} // extern "C"
