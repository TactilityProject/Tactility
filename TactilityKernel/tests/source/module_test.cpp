#include "doctest.h"
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/module.h>

static void symbol_test_function() { /* NO-OP */ }

static error_t test_start_result = ERROR_NONE;
static bool start_called = false;
static struct Module* start_add_order_check_module = nullptr;
static error_t test_start() {
    start_called = true;
    if (start_add_order_check_module != nullptr) {
        // If the module was already added to the ledger before start() runs,
        // a duplicate module_add() must report that it already exists.
        CHECK_EQ(module_add(start_add_order_check_module), ERROR_INVALID_STATE);
    }
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
        .internal = nullptr
    };

    // Test successful construction
    CHECK_EQ(module_construct(&module), ERROR_NONE);
    CHECK_EQ(module_is_started(&module), false);

    // Test successful destruction
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("Module registration") {
    struct Module module = {
        .name = "test",
        .start = test_start,
        .stop = test_stop,
        .symbols = nullptr,
        .internal = nullptr
    };

    // module_add should succeed
    CHECK_EQ(module_add(&module), ERROR_NONE);

    // module_remove should succeed
    CHECK_EQ(module_remove(&module), ERROR_NONE);
}

static int indexed_symbol_one = 1;
static int indexed_symbol_two = 2;

namespace {

/** Releases the index when a failed REQUIRE unwinds the test. */
class IndexSession {
public:
    IndexSession() { module_symbol_index_acquire(); }
    ~IndexSession() { module_symbol_index_release(); }
    IndexSession(const IndexSession&) = delete;
    IndexSession& operator=(const IndexSession&) = delete;
};

/** Destructs the module when a failed REQUIRE unwinds the test. */
class ModuleRegistration {
public:
    explicit ModuleRegistration(Module* module) : module(module) {}
    ~ModuleRegistration() {
        if (module->internal != nullptr) {
            (void)module_ensure_destructed(module);
        }
    }
    ModuleRegistration(const ModuleRegistration&) = delete;
    ModuleRegistration& operator=(const ModuleRegistration&) = delete;

private:
    Module* module;
};

} // namespace

static const struct ModuleSymbol indexed_symbols[] = {
    DEFINE_MODULE_SYMBOL(indexed_symbol_one),
    {"duplicate_symbol", &indexed_symbol_one},
    {"duplicate_symbol", &indexed_symbol_two},
    DEFINE_MODULE_SYMBOL(indexed_symbol_two),
    MODULE_SYMBOL_TERMINATOR
};

TEST_CASE("Module symbol indexes are built on demand and keep the first duplicate") {
    struct Module module = {
        .name = "indexed_test",
        .start = test_start,
        .stop = test_stop,
        .symbols = indexed_symbols,
        .internal = nullptr
    };

    ModuleRegistration registration(&module);
    REQUIRE_EQ(module_construct(&module), ERROR_NONE);
    REQUIRE_EQ(module_add(&module), ERROR_NONE);

    uintptr_t address = 0;
    // Without an index the linear scan still resolves everything.
    REQUIRE(module_resolve_symbol(&module, "indexed_symbol_one", &address));
    CHECK_EQ(address, reinterpret_cast<uintptr_t>(&indexed_symbol_one));

    {
        IndexSession index;

        REQUIRE(module_resolve_symbol(&module, "indexed_symbol_two", &address));
        CHECK_EQ(address, reinterpret_cast<uintptr_t>(&indexed_symbol_two));

        // A name exported twice keeps the first table entry, like the linear scan does.
        REQUIRE(module_resolve_symbol(&module, "duplicate_symbol", &address));
        CHECK_EQ(address, reinterpret_cast<uintptr_t>(&indexed_symbol_one));

        CHECK_FALSE(module_resolve_symbol(&module, "missing_symbol", &address));

        // The global lookup goes through the same index.
        REQUIRE(module_resolve_symbol_global("indexed_symbol_two", &address));
        CHECK_EQ(address, reinterpret_cast<uintptr_t>(&indexed_symbol_two));

        // Reference counted: a nested acquire/release leaves the index in place.
        {
            IndexSession nested;
        }
        REQUIRE(module_resolve_symbol(&module, "indexed_symbol_one", &address));
    }

    // Back on the linear scan, resolution still works.
    REQUIRE(module_resolve_symbol(&module, "indexed_symbol_two", &address));
    CHECK_EQ(address, reinterpret_cast<uintptr_t>(&indexed_symbol_two));

    // Removing a module while the index is active clears its entry.
    {
        IndexSession index;
        REQUIRE_EQ(module_remove(&module), ERROR_NONE);
        CHECK_FALSE(module_resolve_symbol(&module, "indexed_symbol_two", &address));
        CHECK_FALSE(module_resolve_symbol_global("indexed_symbol_two", &address));
    }

    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

static int late_symbol_target = 0;
static const struct ModuleSymbol late_symbols[] = {
    {"late_symbol", &late_symbol_target},
    MODULE_SYMBOL_TERMINATOR
};

TEST_CASE("A module added while indexes are active falls back to the scan") {
    struct Module module = {
        .name = "late_indexed_test",
        .start = test_start,
        .stop = test_stop,
        .symbols = late_symbols,
        .internal = nullptr
    };

    ModuleRegistration registration(&module);
    {
        IndexSession index;
        REQUIRE_EQ(module_construct(&module), ERROR_NONE);
        REQUIRE_EQ(module_add(&module), ERROR_NONE);

        // No room was reserved for it, so this goes through the linear scan.
        uintptr_t address = 0;
        REQUIRE(module_resolve_symbol(&module, "late_symbol", &address));
        CHECK_EQ(address, reinterpret_cast<uintptr_t>(&late_symbol_target));
    }

    // The next acquire covers it.
    {
        IndexSession index;
        uintptr_t address = 0;
        REQUIRE(module_resolve_symbol(&module, "late_symbol", &address));
        CHECK_EQ(address, reinterpret_cast<uintptr_t>(&late_symbol_target));
    }

    REQUIRE_EQ(module_remove(&module), ERROR_NONE);
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
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
        .internal = nullptr
    };

    CHECK_EQ(module_construct(&module), ERROR_NONE);

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

    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("module_start sets itself as owner of its drivers") {
    Driver driver = { .name = "owned_driver" };
    Driver* const drivers[] = { &driver, nullptr };
    struct Module module = {
        .name = "owner_test",
        .drivers = drivers
    };

    CHECK_EQ(module_construct(&module), ERROR_NONE);
    CHECK_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(driver_get_owner(&driver), &module);
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(driver.internal, nullptr);
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("module_stop is refused while a driver of the module is in use") {
    static const char* busy_compatible[] = { "busy", nullptr };
    static Driver driver = {
        .name = "busy_driver",
        .compatible = busy_compatible,
    };
    static Driver* const drivers[] = { &driver, nullptr };
    static Device device = { .name = "busy_device", .config = nullptr, .parent = nullptr };
    stop_called = false;
    test_stop_result = ERROR_NONE;
    struct Module module = {
        .name = "busy_test",
        .stop = test_stop,
        .drivers = drivers
    };

    REQUIRE_EQ(module_construct(&module), ERROR_NONE);
    REQUIRE_EQ(module_start(&module), ERROR_NONE);
    REQUIRE_EQ(device_construct(&device), ERROR_NONE);
    REQUIRE_EQ(device_add(&device), ERROR_NONE);
    REQUIRE_EQ(driver_bind(&driver, &device), ERROR_NONE);
    CHECK(driver_is_in_use(&driver));

    CHECK_EQ(module_stop(&module), ERROR_RESOURCE_BUSY);
    CHECK_EQ(module_is_started(&module), true);
    CHECK_EQ(stop_called, false);

    REQUIRE_EQ(driver_unbind(&driver, &device), ERROR_NONE);
    CHECK_FALSE(driver_is_in_use(&driver));
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(stop_called, true);

    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("module_stop is refused while the module is referenced") {
    static const struct ModuleSymbol symbols[] = {
        DEFINE_MODULE_SYMBOL(symbol_test_function),
        MODULE_SYMBOL_TERMINATOR
    };
    start_called = false;
    stop_called = false;
    test_start_result = ERROR_NONE;
    test_stop_result = ERROR_NONE;
    struct Module module = {
        .name = "referenced_test",
        .start = test_start,
        .stop = test_stop,
        .symbols = symbols,
        .internal = nullptr
    };

    // Not started yet
    REQUIRE_EQ(module_construct(&module), ERROR_NONE);
    CHECK_EQ(module_get(&module), ERROR_INVALID_STATE);
    REQUIRE_EQ(module_add(&module), ERROR_NONE);
    REQUIRE_EQ(module_start(&module), ERROR_NONE);

    // A reference from a symbol lookup
    uintptr_t address = 0;
    struct Module* owner = nullptr;
    REQUIRE(module_resolve_symbol_global_get("symbol_test_function", &address, &owner));
    CHECK_EQ(owner, &module);
    CHECK_EQ(module_get(&module), ERROR_NONE);

    // Refused until both references are released, and it keeps running
    CHECK_EQ(module_stop(&module), ERROR_RESOURCE_BUSY);
    CHECK_EQ(module_is_started(&module), true);
    CHECK_EQ(stop_called, false);
    module_put(&module);
    CHECK_EQ(module_stop(&module), ERROR_RESOURCE_BUSY);
    module_put(owner);

    // Restart is possible after a failed stop attempt and references are refused once stopped
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(stop_called, true);
    CHECK_EQ(module_get(&module), ERROR_INVALID_STATE);

    // The module is still added, so its symbols resolve and keep it from being destructed
    REQUIRE(module_resolve_symbol_global_get("symbol_test_function", &address, &owner));
    CHECK_EQ(module_ensure_destructed(&module), ERROR_RESOURCE_BUSY);
    CHECK_EQ(module_is_started(&module), false);
    module_put(owner);

    CHECK_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(module_get(&module), ERROR_NONE);
    module_put(&module);

    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(module_remove(&module), ERROR_NONE);
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("A code reference keeps a module mapped without preventing a stop") {
    static const struct ModuleSymbol symbols[] = {
        DEFINE_MODULE_SYMBOL(symbol_test_function),
        MODULE_SYMBOL_TERMINATOR
    };
    start_called = false;
    stop_called = false;
    test_start_result = ERROR_NONE;
    test_stop_result = ERROR_NONE;
    struct Module module = {
        .name = "code_referenced_test",
        .start = test_start,
        .stop = test_stop,
        .symbols = symbols,
        .internal = nullptr
    };

    REQUIRE_EQ(module_construct_add_start(&module), ERROR_NONE);

    uintptr_t address = 0;
    struct Module* owner = nullptr;
    REQUIRE(module_resolve_symbol_global_get_code("symbol_test_function", &address, &owner));
    CHECK_EQ(owner, &module);

    // Relocated code only needs the symbols, so it doesn't keep the module running
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(stop_called, true);

    // It does keep the module from being removed or destructed
    CHECK_EQ(module_remove(&module), ERROR_RESOURCE_BUSY);
    CHECK_EQ(module_ensure_destructed(&module), ERROR_RESOURCE_BUSY);

    // And it can be started again while the code reference is held
    CHECK_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(module_is_started(&module), true);

    module_put_code(owner);
    CHECK_EQ(module_ensure_destructed(&module), ERROR_NONE);
}

TEST_CASE("A code reference prevents stopping a module from a package") {
    stop_called = false;
    test_start_result = ERROR_NONE;
    test_stop_result = ERROR_NONE;
    struct Module module = {
        .name = "external_code_referenced_test",
        .start = test_start,
        .stop = test_stop,
        .symbols = nullptr,
        .internal = nullptr
    };

    REQUIRE_EQ(module_construct_add_start(&module), ERROR_NONE);
    REQUIRE_EQ(module_set_external(&module, true), ERROR_NONE);
    REQUIRE_EQ(module_get_code(&module), ERROR_NONE);

    // The user can stop package modules from Kernel settings, so an app's use of one must protect it
    CHECK_EQ(module_stop(&module), ERROR_RESOURCE_BUSY);
    CHECK_EQ(stop_called, false);

    module_put_code(&module);
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(stop_called, true);
    CHECK_EQ(module_remove(&module), ERROR_NONE);
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("module_remove is refused while the module is referenced") {
    static const struct ModuleSymbol symbols[] = {
        DEFINE_MODULE_SYMBOL(symbol_test_function),
        MODULE_SYMBOL_TERMINATOR
    };
    struct Module module = {
        .name = "remove_referenced_test",
        .start = nullptr,
        .stop = nullptr,
        .symbols = symbols,
        .internal = nullptr
    };

    // Stopped but still added, which is when a reference can still be taken
    REQUIRE_EQ(module_construct_add_start(&module), ERROR_NONE);
    REQUIRE_EQ(module_stop(&module), ERROR_NONE);

    uintptr_t address = 0;
    struct Module* owner = nullptr;
    REQUIRE(module_resolve_symbol_global_get("symbol_test_function", &address, &owner));
    CHECK_EQ(owner, &module);

    // Neither removing nor destructing is allowed, and the module can still be resolved
    CHECK_EQ(module_remove(&module), ERROR_RESOURCE_BUSY);
    CHECK_EQ(module_ensure_destructed(&module), ERROR_RESOURCE_BUSY);
    CHECK(module_resolve_symbol(&module, "symbol_test_function", &address));

    module_put(owner);
    CHECK_EQ(module_ensure_destructed(&module), ERROR_NONE);
}

TEST_CASE("module_stop that fails leaves the module referenceable") {
    start_called = false;
    test_start_result = ERROR_NONE;
    test_stop_result = ERROR_NOT_SUPPORTED;
    struct Module module = {
        .name = "failed_stop_test",
        .start = test_start,
        .stop = test_stop,
        .symbols = nullptr,
        .internal = nullptr
    };

    REQUIRE_EQ(module_construct(&module), ERROR_NONE);
    REQUIRE_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(module_stop(&module), ERROR_NOT_SUPPORTED);

    // Still started, and not stuck in a stopping state
    CHECK_EQ(module_get(&module), ERROR_NONE);
    module_put(&module);

    test_stop_result = ERROR_NONE;
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
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
        .internal = nullptr
    };

    REQUIRE_EQ(module_construct(&module), ERROR_NONE);

    uintptr_t addr;
    // Should fail as it is not added
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), false);
    CHECK_EQ(module_resolve_symbol(&module, "symbol_test_function", &addr), false);
    REQUIRE_EQ(module_add(&module), ERROR_NONE);
    // Resolvable once the module is added, also when it has not started yet
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), true);
    CHECK_EQ(module_resolve_symbol(&module, "symbol_test_function", &addr), true);
    REQUIRE_EQ(module_start(&module), ERROR_NONE);
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), true);
    // Cleanup
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(module_remove(&module), ERROR_NONE);

    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("module_ensure_started adds module to global ledger") {
    start_called = false;
    stop_called = false;
    test_start_result = ERROR_NONE;
    test_stop_result = ERROR_NONE;

    static const struct ModuleSymbol test_symbols[] = {
        DEFINE_MODULE_SYMBOL(symbol_test_function),
        MODULE_SYMBOL_TERMINATOR
    };

    struct Module module = {
        .name = "test_ensure_started",
        .start = test_start,
        .stop = test_stop,
        .symbols = test_symbols,
        .internal = nullptr
    };

    uintptr_t addr;
    // Not resolvable before module_ensure_started is called
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), false);

    // test_start() asserts module_add(&module) is already ERROR_INVALID_STATE by the
    // time start() runs, proving module_add happens before module_start (not after).
    start_add_order_check_module = &module;
    CHECK_EQ(module_ensure_started(&module), ERROR_NONE);
    start_add_order_check_module = nullptr;
    CHECK_EQ(module_is_started(&module), true);
    CHECK_EQ(start_called, true);

    // Module must be both added to the ledger and started to be resolvable
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), true);

    // Calling again should be idempotent: no duplicate start, still resolvable
    start_called = false;
    CHECK_EQ(module_ensure_started(&module), ERROR_NONE);
    CHECK_EQ(start_called, false);
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), true);

    // Cleanup
    CHECK_EQ(module_stop(&module), ERROR_NONE);
    CHECK_EQ(module_remove(&module), ERROR_NONE);
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
}

TEST_CASE("module_ensure_destructed removes module from global ledger") {
    start_called = false;
    stop_called = false;
    test_start_result = ERROR_NONE;
    test_stop_result = ERROR_NONE;

    static const struct ModuleSymbol test_symbols[] = {
        DEFINE_MODULE_SYMBOL(symbol_test_function),
        MODULE_SYMBOL_TERMINATOR
    };

    struct Module module = {
        .name = "test_ensure_destructed",
        .start = test_start,
        .stop = test_stop,
        .symbols = test_symbols,
        .internal = nullptr
    };

    CHECK_EQ(module_ensure_started(&module), ERROR_NONE);

    uintptr_t addr;
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), true);

    CHECK_EQ(module_ensure_destructed(&module), ERROR_NONE);
    CHECK_EQ(module_is_started(&module), false);
    CHECK_EQ(stop_called, true);

    // Module must no longer be resolvable once destructed
    CHECK_EQ(module_resolve_symbol_global("symbol_test_function", &addr), false);

    // Directly prove detachment from the ledger: if module_ensure_destructed had left
    // the module in place, this module_add() would return ERROR_INVALID_STATE.
    CHECK_EQ(module_add(&module), ERROR_NONE);
    CHECK_EQ(module_remove(&module), ERROR_NONE);

    // Calling again on an already-destructed module should be a no-op
    CHECK_EQ(module_ensure_destructed(&module), ERROR_NONE);
}

TEST_CASE("Module external flag") {
    struct Module module = {
        .name = "external-test",
        .start = nullptr,
        .stop = nullptr,
        .symbols = nullptr,
        .internal = nullptr
    };

    // Not constructed yet
    CHECK_EQ(module_is_external(&module), false);
    CHECK_EQ(module_set_external(&module, true), ERROR_INVALID_STATE);

    CHECK_EQ(module_construct(&module), ERROR_NONE);
    CHECK_EQ(module_is_external(&module), false);
    CHECK_EQ(module_set_external(&module, true), ERROR_NONE);
    CHECK_EQ(module_is_external(&module), true);

    // The flag does not outlive the construction
    CHECK_EQ(module_destruct(&module), ERROR_NONE);
    CHECK_EQ(module_is_external(&module), false);
}
