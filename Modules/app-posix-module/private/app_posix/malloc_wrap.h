// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#ifndef __APPLE__

/**
 * Sets the address range of the app image running on the calling thread. Allocations made by code
 * in [start, end) are counted for the current app instance (see app/memory.h).
 */
void app_posix_set_current_image(uintptr_t start, uintptr_t end);

#else

// Allocations aren't counted on Apple platforms
inline void app_posix_set_current_image(uintptr_t, uintptr_t) {}

#endif
