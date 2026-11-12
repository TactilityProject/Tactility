// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/memory.h>

#include <cstdint>
#include <cstdlib>
#include <cstddef>

namespace tt {

/** Allocator backed by tactility/memory.h's memory_alloc_with_policy(), with Required/Desired as
 * its required/desired MemoryCapability flags (bitwise-OR'd, e.g. MEMORY_CAPABILITY_EXTERNAL). */
template<typename T, uint16_t Required = 0, uint16_t Desired = 0>
struct Allocator {
    using value_type = T;

    // libstdc++'s default rebind_alloc only pattern-matches allocator templates whose parameters
    // are all types, which Required/Desired (non-type) breaks; spelling it out here restores it.
    template<typename U> struct rebind { using other = Allocator<U, Required, Desired>; };

    Allocator() noexcept = default;
    template<typename U> constexpr Allocator(const Allocator<U, Required, Desired>&) noexcept {}

    T* allocate(std::size_t n) {
        const MemoryPolicy policy { .required = Required, .desired = Desired, .alignment = alignof(T) };
        void* ptr = memory_alloc_with_policy(n * sizeof(T), &policy);
        if (ptr == nullptr) {
            std::abort(); // Exceptions are disabled project-wide, so OOM can't be signalled via std::bad_alloc.
        }
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept {
        memory_free(ptr);
    }
};

template<typename T, typename U, uint16_t Required, uint16_t Desired>
bool operator==(const Allocator<T, Required, Desired>&, const Allocator<U, Required, Desired>&) noexcept { return true; }

/** Prefers external memory, falling back to internal RAM when unavailable. */
template<typename T>
using OptExternalAllocator = Allocator<T, 0, MEMORY_CAPABILITY_EXTERNAL>;

} // namespace tt
