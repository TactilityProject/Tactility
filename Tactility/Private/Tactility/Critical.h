#pragma once

#include <cstdint>

namespace tt::kernel::critical {

struct CriticalInfo {
    uint32_t isrm;
    bool fromIsr;
    bool kernelRunning;
};

/** Enter a critical section
 * @return info on the status
 */
CriticalInfo enter();

/**
 * Exit a critical section
 * @param[in] info the info from when the critical section was started
 */
void exit(CriticalInfo info);

} // namespace
