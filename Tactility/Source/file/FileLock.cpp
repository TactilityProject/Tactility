#include "Tactility/file/FileLock.h"

#include <Tactility/hal/SdCard.h>

namespace tt::file {

std::shared_ptr<Lock> findLock(const std::string& path) {
    return hal::sdcard::findSdCardLock(path);
}

}
