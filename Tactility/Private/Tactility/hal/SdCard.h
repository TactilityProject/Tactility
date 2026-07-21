#pragma once

#include <memory.h>
#include <string.h>

#include <Tactility/Lock.h>

namespace tt::hal::sdcard {

/**
 * Attempt to find an SD card that the specified belongs to,
 * and returns its lock if the SD card is mounted. Otherwise it returns nullptr.
 * @param[in] a path on a file system (e.g. file, directory, etc.)
 * @return the lock of a mounted SD card or otherwise null
 */
std::shared_ptr<Lock> findSdCardLock(const std::string& path);

void mountAll();

}
