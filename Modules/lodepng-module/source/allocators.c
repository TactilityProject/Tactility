// SPDX-License-Identifier: Apache-2.0
#include <lodepng/lodepng.h>

#include <tactility/memory.h>

static const struct MemoryPolicy EXTERNAL_PREFERRED = {
    .required = 0,
    .desired = MEMORY_CAPABILITY_EXTERNAL,
    .alignment = 0,
};

void* lodepng_malloc(size_t size) {
    return memory_alloc_with_policy(size, &EXTERNAL_PREFERRED);
}

void* lodepng_realloc(void* ptr, size_t new_size) {
    return memory_realloc_with_policy(ptr, new_size, &EXTERNAL_PREFERRED);
}

void lodepng_free(void* ptr) {
    memory_free(ptr);
}
