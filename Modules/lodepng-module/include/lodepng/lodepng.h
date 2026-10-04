// SPDX-License-Identifier: Apache-2.0
#pragma once

// The C++ wrapper is not compiled: callers use the C API from C and C++ alike
#define LODEPNG_NO_COMPILE_CPP
// Allocations go through the kernel's memory API (see lodepng_malloc() below)
#define LODEPNG_NO_COMPILE_ALLOCATORS

#ifdef __cplusplus
extern "C" {
#endif

#include <lodepng/lodepng_upstream.h>

/** Allocates memory, preferring external memory (PSRAM) */
void* lodepng_malloc(size_t size);

void* lodepng_realloc(void* ptr, size_t new_size);

/** Releases memory from lodepng_malloc(), including images returned by the decode and encode functions */
void lodepng_free(void* ptr);

#ifdef __cplusplus
}
#endif
