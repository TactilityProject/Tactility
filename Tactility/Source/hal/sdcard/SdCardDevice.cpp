#include <Tactility/hal/sdcard/SdCardDevice.h>

#include <tactility/filesystem/file_system.h>

#include <cstring>

namespace tt::hal::sdcard {

static error_t mount(void* data) {
    auto* device = static_cast<SdCardDevice*>(data);
    auto path = device->getMountPath();
    if (!device->mount(path)) return ERROR_UNDEFINED;
    return ERROR_NONE;
}

static error_t unmount(void* data) {
    auto* device = static_cast<SdCardDevice*>(data);
    if (!device->unmount()) return ERROR_UNDEFINED;
    return ERROR_NONE;
}

static bool is_mounted(void* data) {
    auto* device = static_cast<SdCardDevice*>(data);
    return device->isMounted();
}

static error_t get_path(void* data, char* out_path, size_t out_path_size) {
    auto* device = static_cast<SdCardDevice*>(data);
    const auto mount_path = device->getMountPath();
    if (mount_path.size() >= out_path_size) return ERROR_BUFFER_OVERFLOW;
    if (mount_path.empty()) return ERROR_INVALID_STATE;
    strncpy(out_path, mount_path.c_str(), out_path_size);
    return ERROR_NONE;
}

FileSystemApi sdCardDeviceApi = {
    .mount = mount,
    .unmount = unmount,
    .is_mounted = is_mounted,
    .get_path = get_path
};

SdCardDevice::SdCardDevice(MountBehaviour mountBehaviour) : mountBehaviour(mountBehaviour) {
    fileSystem = file_system_add(&sdCardDeviceApi, this);
    check(fileSystem != nullptr);
}

SdCardDevice::~SdCardDevice() {
    file_system_remove(fileSystem);
}

}
