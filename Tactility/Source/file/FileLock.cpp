#include "Tactility/file/FileLock.h"

#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <Tactility/Mutex.h>

namespace tt::file {

std::shared_ptr<Lock> findLock(const std::string& path) {
    return hal::sdcard::findSdCardLock(path);
}

}
