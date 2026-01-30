#include "doctest.h"
#include <tactility/module.h>

static error_t test_start_result = ERROR_NONE;
static bool start_called = false;
static error_t test_start() {
    start_called = true;
    return test_start_result;
}

static error_t test_stop_result = ERROR_NONE;
static bool stop_called = false;
static error_t test_stop() {
    stop_called = true;
    return test_stop_result;
}

TEST_CASE("ModuleParent construction and destruction") {
    struct ModuleParent parent = { "test_parent", nullptr };

    // Test successful construction
    CHECK_EQ(module_parent_construct(&parent), ERROR_NONE);
    CHECK_NE(parent.module_parent_private, nullptr);

    // Test successful destruction
    CHECK_EQ(module_parent_destruct(&parent), ERROR_NONE);
    CHECK_EQ(parent.module_parent_private, nullptr);
}

TEST_CASE("ModuleParent destruction with children") {
    struct ModuleParent parent = { "parent", nullptr };
    REQUIRE_EQ(module_parent_construct(&parent), ERROR_NONE);

    struct Module module = {
        .name = "test",
        .start = test_start,
        .stop = test_stop,
        .internal = {.started = false, .parent = nullptr}
    };

    REQUIRE_EQ(module_set_parent(&module, &parent), ERROR_NONE);

    // Should fail to destruct because it has a child
    CHECK_EQ(module_parent_destruct(&parent), ERROR_INVALID_STATE);
    CHECK_NE(parent.module_parent_private, nullptr);

    // Remove child
    REQUIRE_EQ(module_set_parent(&module, nullptr), ERROR_NONE);

    // Now it should succeed
    CHECK_EQ(module_parent_destruct(&parent), ERROR_NONE);
    CHECK_EQ(parent.module_parent_private, nullptr);
}

TEST_CASE("Module parent management") {
    struct ModuleParent parent1 = { "parent1", nullptr };
    struct ModuleParent parent2 = { "parent2", nullptr };
    REQUIRE_EQ(module_parent_construct(&parent1), ERROR_NONE);
    REQUIRE_EQ(module_parent_construct(&parent2), ERROR_NONE);

    struct Module module = {
        .name = "test",
        .start = test_start,
        .stop = test_stop,
        .internal = {.started = false, .parent = nullptr}
    };

    // Set parent
    CHECK_EQ(module_set_parent(&module, &parent1), ERROR_NONE);
    CHECK_EQ(module.internal.parent, &parent1);

    // Change parent
    CHECK_EQ(module_set_parent(&module, &parent2), ERROR_NONE);
    CHECK_EQ(module.internal.parent, &parent2);

    // Clear parent
    CHECK_EQ(module_set_parent(&module, nullptr), ERROR_NONE);
    CHECK_EQ(module.internal.parent, nullptr);

    // Set same parent (should be NOOP and return ERROR_NONE)
    CHECK_EQ(module_set_parent(&module, &parent1), ERROR_NONE);
    CHECK_EQ(module_set_parent(&module, &parent1), ERROR_NONE);
    CHECK_EQ(module.internal.parent, &parent1);

    CHECK_EQ(module_set_parent(&module, nullptr), ERROR_NONE);
    CHECK_EQ(module_parent_destruct(&parent1), ERROR_NONE);
    CHECK_EQ(module_parent_destruct(&parent2), ERROR_NONE);
}

TEST_CASE("Module lifecycle") {
    struct ModuleParent parent = { "parent", nullptr };
    REQUIRE_EQ(module_parent_construct(&parent), ERROR_NONE);

    start_called = false;
    stop_called = false;
    test_start_result = ERROR_NONE;
    test_stop_result = ERROR_NONE;

    struct Module module = {
        .name = "test",
        .start = test_start,
        .stop = test_stop,
        .internal = {.started = false, .parent = nullptr}
    };

    // 1. Cannot start without parent
    CHECK_EQ(module_start(&module), ERROR_INVALID_STATE);
    CHECK_EQ(module_is_started(&module), false);
    CHECK_EQ(start_called, false);

    CHECK_EQ(module_set_parent(&module, &parent), ERROR_NONE);

    // 2. Successful start
    CHECK_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(module_is_started(&module), true);
    CHECK_EQ(start_called, true);

    // 3. Start when already started (should return ERROR_NONE)
    start_called = false;
    CHECK_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(start_called, false); // start() function should NOT be called again

    // 4. Cannot change parent while started
    CHECK_EQ(module_set_parent(&module, nullptr), ERROR_INVALID_STATE);

    // 5. Successful stop
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(module_is_started(&module), false);
    CHECK_EQ(stop_called, true);

    // 6. Stop when already stopped (should return ERROR_NONE)
    stop_called = false;
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(stop_called, false); // stop() function should NOT be called again

    // 7. Test failed start
    test_start_result = ERROR_NOT_FOUND;
    start_called = false;
    CHECK_EQ(module_start(&module), ERROR_NOT_FOUND);
    CHECK_EQ(module_is_started(&module), false);
    CHECK_EQ(start_called, true);

    // 8. Test failed stop
    CHECK_EQ(module_set_parent(&module, &parent), ERROR_NONE);
    test_start_result = ERROR_NONE;
    CHECK_EQ(module_start(&module), ERROR_NONE);

    test_stop_result = ERROR_NOT_SUPPORTED;
    stop_called = false;
    CHECK_EQ(module_stop(&module), ERROR_NOT_SUPPORTED);
    CHECK_EQ(module_is_started(&module), true); // Should still be started if stop failed
    CHECK_EQ(stop_called, true);

    // Clean up: fix stop result so we can stop it
    test_stop_result = ERROR_NONE;
    CHECK_EQ(module_stop(&module), ERROR_NONE);

    CHECK_EQ(module_set_parent(&module, nullptr), ERROR_NONE);
    CHECK_EQ(module_parent_destruct(&parent), ERROR_NONE);
}
