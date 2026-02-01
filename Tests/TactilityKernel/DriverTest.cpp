#include "doctest.h"

#include <tactility/driver.h>
#include <tactility/module.h>

static Module module = {
    .name = "test_module",
    .start = nullptr,
    .stop = nullptr
};

TEST_CASE("driver_construct and driver_destruct should set and unset the correct fields") {
    Driver driver = { 0 };
    driver.owner = &module;

    CHECK_EQ(driver_construct(&driver), ERROR_NONE);
    CHECK_EQ(driver_add(&driver), ERROR_NONE);
    CHECK_NE(driver.driver_private, nullptr);
    CHECK_EQ(driver_remove(&driver), ERROR_NONE);
    CHECK_EQ(driver_destruct(&driver), ERROR_NONE);
    CHECK_EQ(driver.driver_private, nullptr);
}

TEST_CASE("a driver without a module should not be destructible") {
    Driver driver = { 0 };

    CHECK_EQ(driver_construct(&driver), ERROR_NONE);
    CHECK_EQ(driver_destruct(&driver), ERROR_NOT_ALLOWED);
    driver.owner = &module;
    CHECK_EQ(driver_destruct(&driver), ERROR_NONE);
}

TEST_CASE("driver_is_compatible should return true if a compatible value is found") {
    const char* compatible[] = { "test_compatible", nullptr };
    Driver driver = {
        .name = "test_driver",
        .compatible = compatible,
        .start_device = nullptr,
        .stop_device = nullptr,
        .api = nullptr,
        .device_type = nullptr,
        .owner = &module,
        .driver_private = nullptr
    };
    CHECK_EQ(driver_is_compatible(&driver, "test_compatible"), true);
    CHECK_EQ(driver_is_compatible(&driver, "nope"), false);
    CHECK_EQ(driver_is_compatible(&driver, nullptr), false);
}

TEST_CASE("driver_find should only find a compatible driver when the driver was constructed") {
    const char* compatible[] = { "test_compatible", nullptr };
    Driver driver = {
        .name = "test_driver",
        .compatible = compatible,
        .start_device = nullptr,
        .stop_device = nullptr,
        .api = nullptr,
        .device_type = nullptr,
        .owner = &module,
        .driver_private = nullptr
    };

    Driver* found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, nullptr);

    CHECK_EQ(driver_construct(&driver), ERROR_NONE);
    CHECK_EQ(driver_add(&driver), ERROR_NONE);

    found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, &driver);

    CHECK_EQ(driver_remove(&driver), ERROR_NONE);
    CHECK_EQ(driver_destruct(&driver), ERROR_NONE);

    found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, nullptr);
}
