// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#ifndef __APPLE__

/**
 * Sets the address range of the app image running on the calling thread. Allocations made by code
 * in [start, end) are counted for the current app instance (see app/memory.h).
 */
void app_posix_set_current_image(uintptr_t start, uintptr_t end);

/** Gets the range set by app_posix_set_current_image() on the calling thread, for a thread the app creates. */
void app_posix_get_current_image(uintptr_t* out_start, uintptr_t* out_end);

/** @return true if @a caller lies inside the image of the app running on the calling thread */
bool app_posix_is_app_caller(const void* caller);

#else

// Allocations aren't counted, and resources aren't tracked, on Apple platforms
inline void app_posix_set_current_image(uintptr_t, uintptr_t) {}

inline void app_posix_get_current_image(uintptr_t* out_start, uintptr_t* out_end) {
    *out_start = 0;
    *out_end = 0;
}

inline bool app_posix_is_app_caller(const void*) { return false; }

#endif
