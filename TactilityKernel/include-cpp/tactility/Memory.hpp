// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/memory.h>

#include <new>
#include <utility>

namespace tt {

/** Construct an object in external memory (PSRAM) when available. @return nullptr when out of memory */
template<typename T, typename... Args>
T* newObject(Args&&... args) {
    constexpr MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = alignof(T) };
    void* memory = memory_alloc_with_policy(sizeof(T), &policy);
    return (memory != nullptr) ? new (memory) T(::std::forward<Args>(args)...) : nullptr;
}

/** Destroy an object from newObject() */
template<typename T>
void deleteObject(T* object) {
    object->~T();
    memory_free(object);
}

} // namespace tt
