#include <tactility/hal/Device.h>
#include "Tactility/hal/sdcard/SdCardDevice.h"

namespace tt::hal::sdcard {

std::shared_ptr<SdCardDevice> _Nullable find(const std::string& path) {
    auto sdcards = findDevices<SdCardDevice>(Device::Type::SdCard);
    for (auto& sdcard : sdcards) {
        if (sdcard->isMounted() && path.starts_with(sdcard->getMountPath())) {
            return sdcard;
        }
    }

    return nullptr;
}

std::shared_ptr<Lock> findSdCardLock(const std::string& path) {
    auto sdcard = find(path);
    if (sdcard != nullptr) {
        return sdcard->getLock();
    }

    return nullptr;
}

}
