#include "doctest.h"
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/module.h>

static Module hotplug_test_module = {
    .name = "hotplug_test_module",
    .start = nullptr,
    .stop = nullptr
};

static bool probe_present = false;
static int probe_driver_start_called = 0;
static int probe_driver_stop_called = 0;

static bool probe_driver_probe(Device*) { return probe_present; }
static error_t probe_driver_start(Device*) { probe_driver_start_called++; return ERROR_NONE; }
static error_t probe_driver_stop(Device*) { probe_driver_stop_called++; return ERROR_NONE; }

static Driver probe_driver = {
    .name = "hotplug_test_probe_driver",
    .compatible = (const char*[]) { "hotplug_test,probe", nullptr },
    .start_device = probe_driver_start,
    .stop_device = probe_driver_stop,
    .probe = probe_driver_probe,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &hotplug_test_module,
    .internal = nullptr,
};

TEST_CASE("device_hotplug_poll_once starts/stops a probe-capable device after debounce") {
    probe_present = false;
    probe_driver_start_called = 0;
    probe_driver_stop_called = 0;

    static Device device {
        .name = "hotplug_probe_device",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&probe_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&device, "hotplug_test,probe"), ERROR_NONE);
    device_hotplug_register(&device);

    // Not present: repeated polling never starts it.
    CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
    CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
    CHECK_EQ(probe_driver_start_called, 0);
    CHECK_FALSE(device_is_ready(&device));

    // Present: needs DEVICE_HOTPLUG_DEBOUNCE_COUNT consecutive polls before it actually starts.
    probe_present = true;
    for (int i = 0; i < DEVICE_HOTPLUG_DEBOUNCE_COUNT - 1; i++) {
        CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
        CHECK_FALSE(device_is_ready(&device));
    }
    CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
    CHECK(device_is_ready(&device));
    CHECK_EQ(probe_driver_start_called, 1);

    // Already started: further polls while still present don't restart it.
    CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
    CHECK_EQ(probe_driver_start_called, 1);

    // Absent again: needs the same debounce before it stops.
    probe_present = false;
    for (int i = 0; i < DEVICE_HOTPLUG_DEBOUNCE_COUNT - 1; i++) {
        CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
        CHECK(device_is_ready(&device));
    }
    CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
    CHECK_FALSE(device_is_ready(&device));
    CHECK_EQ(probe_driver_stop_called, 1);

    // Only started/stopped, never removed/destructed.
    CHECK(device_is_added(&device));

    device_hotplug_unregister(&device);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&probe_driver), ERROR_NONE);
}

static int mandatory_driver_start_called = 0;

static error_t mandatory_driver_start(Device*) { mandatory_driver_start_called++; return ERROR_NONE; }
static error_t mandatory_driver_stop(Device*) { return ERROR_NONE; }

static Driver mandatory_driver = {
    .name = "hotplug_test_mandatory_driver",
    .compatible = (const char*[]) { "hotplug_test,mandatory", nullptr },
    .start_device = mandatory_driver_start,
    .stop_device = mandatory_driver_stop,
    .probe = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &hotplug_test_module,
    .internal = nullptr,
};

TEST_CASE("device_hotplug_poll_once starts a mandatory (probe == NULL) device unconditionally on the first call") {
    mandatory_driver_start_called = 0;

    static Device device {
        .name = "hotplug_mandatory_device",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&mandatory_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&device, "hotplug_test,mandatory"), ERROR_NONE);
    device_hotplug_register(&device);

    CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
    CHECK(device_is_ready(&device));
    CHECK_EQ(mandatory_driver_start_called, 1);

    // Already started: further polls don't restart it.
    CHECK_EQ(device_hotplug_poll_once(), ERROR_NONE);
    CHECK_EQ(mandatory_driver_start_called, 1);

    device_hotplug_unregister(&device);
    CHECK_EQ(device_stop(&device), ERROR_NONE);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&mandatory_driver), ERROR_NONE);
}
