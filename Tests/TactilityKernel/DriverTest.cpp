#include "doctest.h"
#include <Tactility/Driver.h>

TEST_CASE("driver_construct and driver_destruct should set and unset the correct fields") {
    Driver driver = { 0 };

    int error = driver_construct(&driver);
    CHECK_EQ(error, 0);
    CHECK_NE(driver.internal.data, nullptr);

    error = driver_destruct(&driver);
    CHECK_EQ(error, 0);
    CHECK_EQ(driver.internal.data, nullptr);
}

TEST_CASE("driver_is_compatible should return true if a compatible value is found") {
    Driver driver = {
        .name = "test_driver",
        .compatible = (const char*[]) { "test_compatible", nullptr },
        .start_device = nullptr,
        .stop_device = nullptr,
        .api = nullptr,
        .device_type = nullptr,
        .internal = { 0 }
    };
    CHECK_EQ(driver_is_compatible(&driver, "test_compatible"), true);
    CHECK_EQ(driver_is_compatible(&driver, "nope"), false);
    CHECK_EQ(driver_is_compatible(&driver, nullptr), false);
}

TEST_CASE("driver_is_compatible should return true if a compatible value is found") {
    Driver driver = {
        .name = "test_driver",
        .compatible = nullptr,
        .start_device = nullptr,
        .stop_device = nullptr,
        .api = nullptr,
        .device_type = nullptr,
        .internal = { 0 }
    };
    CHECK_EQ(driver_is_compatible(&driver, nullptr), false);
}

TEST_CASE("driver_find should only find a compatible driver when the driver was constructed") {
    // Must be static or outside of function to prevent SIGSEV crash due to memory corruption while iterating .compatible
    static Driver driver = {
        .name = "test_driver",
        .compatible = (const char*[]) { "test_compatible", nullptr },
        .start_device = nullptr,
        .stop_device = nullptr,
        .api = nullptr,
        .device_type = nullptr,
        .internal = { 0 }
    };

    Driver* found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, nullptr);

    int error = driver_construct(&driver);
    CHECK_EQ(error, 0);

    found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, &driver);

    error = driver_destruct(&driver);
    CHECK_EQ(error, 0);

    found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, nullptr);
}
