#include "doctest.h"
#include <tactility/memory.h>

#include <cstdint>
#include <cstring>

TEST_CASE("MEMORY_POLICY_DEFAULT should have no requirements") {
    CHECK_EQ(MEMORY_POLICY_DEFAULT.required, 0);
    CHECK_EQ(MEMORY_POLICY_DEFAULT.desired, 0);
    CHECK_EQ(MEMORY_POLICY_DEFAULT.alignment, 0);
}

TEST_CASE("memory_alloc should return usable memory") {
    void* ptr = memory_alloc(64);
    REQUIRE_NE(ptr, nullptr);
    memset(ptr, 0xAB, 64);
    CHECK_EQ(static_cast<uint8_t*>(ptr)[0], 0xAB);
    CHECK_EQ(static_cast<uint8_t*>(ptr)[63], 0xAB);
    memory_free(ptr);
}

TEST_CASE("memory_calloc should zero-initialize memory") {
    auto* ptr = static_cast<uint8_t*>(memory_calloc(16, sizeof(uint8_t)));
    REQUIRE_NE(ptr, nullptr);
    for (size_t i = 0; i < 16; i++) {
        CHECK_EQ(ptr[i], 0);
    }
    memory_free(ptr);
}

TEST_CASE("memory_realloc should preserve contents when growing") {
    auto* ptr = static_cast<uint8_t*>(memory_alloc(8));
    REQUIRE_NE(ptr, nullptr);
    for (uint8_t i = 0; i < 8; i++) {
        ptr[i] = i;
    }

    auto* grown = static_cast<uint8_t*>(memory_realloc(ptr, 32));
    REQUIRE_NE(grown, nullptr);
    for (uint8_t i = 0; i < 8; i++) {
        CHECK_EQ(grown[i], i);
    }

    memory_free(grown);
}

TEST_CASE("memory_realloc with a NULL pointer should behave like an allocation") {
    void* ptr = memory_realloc(nullptr, 32);
    REQUIRE_NE(ptr, nullptr);
    memset(ptr, 0, 32);
    memory_free(ptr);
}

TEST_CASE("memory_free with a NULL pointer should be a no-op") {
    memory_free(nullptr);
}

TEST_CASE("memory_alloc_with_policy should honor a power-of-2 alignment") {
    MemoryPolicy policy = MEMORY_POLICY_DEFAULT;
    policy.alignment = 64;

    void* ptr = memory_alloc_with_policy(128, &policy);
    REQUIRE_NE(ptr, nullptr);
    CHECK_EQ(reinterpret_cast<uintptr_t>(ptr) % 64, 0);
    memory_free(ptr);
}

TEST_CASE("memory_calloc_with_policy should honor alignment and zero-initialize") {
    MemoryPolicy policy = MEMORY_POLICY_DEFAULT;
    policy.alignment = 32;

    auto* ptr = static_cast<uint8_t*>(memory_calloc_with_policy(8, sizeof(uint32_t), &policy));
    REQUIRE_NE(ptr, nullptr);
    CHECK_EQ(reinterpret_cast<uintptr_t>(ptr) % 32, 0);
    for (size_t i = 0; i < 8 * sizeof(uint32_t); i++) {
        CHECK_EQ(ptr[i], 0);
    }
    memory_free(ptr);
}

TEST_CASE("memory_log_stats should not crash") {
    memory_log_stats();
}
