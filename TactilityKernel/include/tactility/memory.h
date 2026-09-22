#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Capability flags that describe what a memory allocation needs or prefers. */
enum MemoryCapability {
    /** Internal memory (non-external/non-PSRAM) memory. */
    MEMORY_CAPABILITY_INTERNAL   = 1u << 0,
    /** External memory (e.g. PSRAM/SPIRAM). */
    MEMORY_CAPABILITY_EXTERNAL   = 1u << 1,
    /** Usable for code execution. */
    MEMORY_CAPABILITY_EXECUTABLE = 1u << 2,
    /** Usable as a DMA source/destination. */
    MEMORY_CAPABILITY_DMA        = 1u << 3,
    /** Usable for SIMD instructions. */
    MEMORY_CAPABILITY_SIMD       = 1u << 4,
};

/**
 * @brief Describes the constraints an allocation must (or should) satisfy.
 *
 * `required` capabilities must all be satisfied or the allocation fails. `desired`
 * capabilities are attempted alongside `required`, but implementations fall back to
 * `required`-only if `required | desired` together can't be satisfied.
 */
struct MemoryPolicy {
    /** A bitset of MemoryCapability flags that are required during allocation. */
    uint16_t required;
    /** A bitset of MemoryCapability flags that are preferable (but optional) during allocation. */
    uint16_t desired;
    /** Alignment (in bytes) of the returned pointer, or 0 for the platform default. Must be a power of 2. */
    size_t alignment;
};

/** The default policy: no required/desired capabilities, no alignment requirement. */
extern const struct MemoryPolicy MEMORY_POLICY_DEFAULT;

/**
 * @brief Writes current heap usage to log at info level.
 * No-op on platforms without heap capability tracking.
 */
void memory_log_stats(void);

/**
 * @brief Total size of the main heap, in bytes.
 * @return the heap's total size, or 0 if unavailable on this platform
 */
size_t memory_heap_total(void);

/**
 * @brief Currently free space in the main heap, in bytes.
 * @return the heap's free size, or 0 if unavailable on this platform
 */
size_t memory_heap_free(void);

/**
 * @brief Allocates memory that satisfies the given policy.
 * @param[in] size number of bytes to allocate
 * @param[in] policy the allocation constraints
 * @return the allocated memory, or NULL on failure
 */
void* memory_alloc_with_policy(size_t size, const struct MemoryPolicy* policy);

/**
 * @brief Resizes a previous allocation, preserving its contents up to the smaller of the old and new size.
 * @warning policy->alignment is not guaranteed to be preserved across a realloc - it is only
 * honored on fresh allocations (memory_alloc_with_policy()/memory_calloc_with_policy()).
 * @param[in] ptr memory previously returned by memory_alloc_with_policy(), memory_calloc_with_policy(),
 * or memory_realloc_with_policy(), or NULL to allocate a new block
 * @param[in] size new memory size in bytes
 * @param[in] policy the policy for the new allocation
 * @return the (possibly moved) allocated memory, or NULL on failure - in which case ptr is left untouched
 */
void* memory_realloc_with_policy(void* ptr, size_t size, const struct MemoryPolicy* policy);

/**
 * @brief Allocates zero-initialized memory that satisfies the given policy.
 * @param[in] count number of elements
 * @param[in] size size of each element in bytes
 * @param[in] policy the allocation constraints
 * @return the allocated memory, or NULL on failure
 */
void* memory_calloc_with_policy(size_t count, size_t size, const struct MemoryPolicy* policy);

/**
 * @brief Allocates memory using MEMORY_POLICY_DEFAULT.
 * @param[in] size number of bytes to allocate
 * @return the allocated memory, or NULL on failure
 */
inline void* memory_alloc(size_t size) {
    return memory_alloc_with_policy(size, &MEMORY_POLICY_DEFAULT);
}

/**
 * @brief Allocates zero-initialized memory using MEMORY_POLICY_DEFAULT.
 * @param[in] count number of elements
 * @param[in] size size of each element in bytes
 * @return the allocated memory, or NULL on failure
 */
inline void* memory_calloc(size_t count, size_t size) {
    return memory_calloc_with_policy(count, size, &MEMORY_POLICY_DEFAULT);
}

/**
 * @brief Resizes a previous allocation using MEMORY_POLICY_DEFAULT. See memory_realloc_with_policy().
 * @param[in] ptr memory previously returned by one of the memory_* allocation functions, or NULL
 * @param[in] size new memory size in bytes
 * @return the (possibly moved) allocated memory, or NULL on failure - in which case ptr is left untouched
 */
inline void* memory_realloc(void* ptr, size_t size) {
    return memory_realloc_with_policy(ptr, size, &MEMORY_POLICY_DEFAULT);
}

/**
 * @brief Frees memory previously returned by one of the memory_* allocation functions.
 * @param[in] ptr the memory to free, or NULL (a no-op)
 */
void memory_free(void* ptr);

#ifdef __cplusplus
}
#endif
