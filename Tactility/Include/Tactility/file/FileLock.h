#pragma once

#include <Tactility/Lock.h>

#include <memory>
#include <string>

/**
 * Some file systems belong to devices on a shared bus (e.g. SPI SD card).
 * Because of the shared bus, a lock is required for its operation.
 */
namespace tt::file {

/**
 * @param[in] path the path to find a lock for
 * @return a lock instance when a lock was found, otherwise nullptr
 */
std::shared_ptr<Lock> findLock(const std::string& path);

}
