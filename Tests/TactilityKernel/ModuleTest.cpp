#include "doctest.h"
#include <tactility/module.h>

static void symbol_test_function() { /* NO-OP */ }

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

TEST_CASE("Module construction and destruction") {
    struct Module module = {
        .name = "test",
        .start = test_start,
        .stop = test_stop,
        .symbols = nullptr,
        .internal = {.started = false}
    };

    // Test successful construction
    CHECK_EQ(module_construct(&module), ERROR_NONE);
    CHECK_EQ(module.internal.started, false);

    // Test successful destruction
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("Module registration") {
    struct Module module = {
        .name = "test",
        .start = test_start,
        .stop = test_stop,
        .symbols = nullptr,
        .internal = {.started = false}
    };

    // module_add should succeed
    CHECK_EQ(module_add(&module), ERROR_NONE);

    // module_remove should succeed
    CHECK_EQ(module_remove(&module), ERROR_NONE);
}

TEST_CASE("Module lifecycle") {
    start_called = false;
    stop_called = false;
    test_start_result = ERROR_NONE;
    test_stop_result = ERROR_NONE;

    struct Module module = {
        .name = "test",
        .start = test_start,
        .stop = test_stop,
        .symbols = nullptr,
        .internal = {.started = false}
    };

    // 1. Successful start (no parent required anymore)
    CHECK_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(module_is_started(&module), true);
    CHECK_EQ(start_called, true);

    // Start when already started (should return ERROR_NONE)
    start_called = false;
    CHECK_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(start_called, false); // start() function should NOT be called again

    // Stop successful
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(module_is_started(&module), false);
    CHECK_EQ(stop_called, true);

    // Stop when already stopped (should return ERROR_NONE)
    stop_called = false;
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(stop_called, false); // stop() function should NOT be called again

    // Test failed start
    test_start_result = ERROR_NOT_FOUND;
    start_called = false;
    CHECK_EQ(module_start(&module), ERROR_NOT_FOUND);
    CHECK_EQ(module_is_started(&module), false);
    CHECK_EQ(start_called, true);

    // Test failed stop
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
}

TEST_CASE("Global symbol resolution") {
    static const struct ModuleSymbol test_symbols[] = {
        DEFINE_MODULE_SYMBOL(symbol_test_function),
        MODULE_SYMBOL_TERMINATOR
    };

    struct Module module = {
        .name = "test_sym",
        .start = test_start,
        .stop = test_stop,
        .symbols = test_symbols,
        .internal = {.started = false}
    };

    uintptr_t addr;
    // Should fail as it is not added or started
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), false);
    REQUIRE_EQ(module_add(&module), ERROR_NONE);
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), false);
    REQUIRE_EQ(module_start(&module), ERROR_NONE);
    // Still fails as symbols are null
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), true);

    // Cleanup
    CHECK_EQ(module_remove(&module), ERROR_NONE);
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}
