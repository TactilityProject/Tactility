#include <Tactility/lvgl/LvglSync.h>

#include <tactility/device.h>
#include <tactility/drivers/sdcard.h>
#include <tactility/filesystem/file_system.h>

#include <string>
#include <memory>

namespace tt::hal::sdcard {

std::shared_ptr<Lock> findSdCardLock(const std::string& path) {
    struct Ctx {
        const std::string& path;
        std::shared_ptr<Lock> result;
    };
    Ctx ctx = { path, nullptr };

    file_system_for_each(&ctx, [](FileSystem* fs, void* context) {
        auto* c = static_cast<Ctx*>(context);
        char mount_path[64];
        if (file_system_get_path(fs, mount_path, sizeof(mount_path)) != ERROR_NONE) return true;
        if (!c->path.starts_with(mount_path)) return true;

        auto* owner = file_system_get_owner(fs);
        if (owner != nullptr) {
            // Check for I2C controller: if it has more than 2 children, assume it's the display
            // TODO: Improve this
            auto* parent = device_get_parent(owner);
            if (parent != nullptr && device_get_child_count(parent) >= 2) {
                c->result = lvgl::getSyncLock();
            }
        }
        return false;
    });

    return ctx.result;
}

void mountAll() {
    device_for_each_of_type(&SDCARD_TYPE, nullptr, [](::Device* device, void*) -> bool {
        if (!device_is_ready(device)) {
            if (device_start(device) != ERROR_NONE) {
            }
        }
        return true;
    });
}

}
