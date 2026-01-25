#include "doctest.h"
#include <Tactility/Driver.h>

TEST_CASE("driver_construct and driver_destruct should set and unset the correct fields") {
    Driver driver = { 0 };

    CHECK_EQ(driver_construct(&driver), ERROR_NONE);
    CHECK_NE(driver.internal.data, nullptr);
    CHECK_EQ(driver_destruct(&driver), ERROR_NONE);
    CHECK_EQ(driver.internal.data, nullptr);
}

TEST_CASE("driver_is_compatible should return true if a compatible value is found") {
    const char* compatible[] = { "test_compatible", nullptr };
    Driver driver = {
        .name = "test_driver",
        .compatible = compatible,
        .startDevice = nullptr,
        .stopDevice = nullptr,
        .api = nullptr,
        .deviceType = nullptr,
        .internal = { 0 }
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
        .startDevice = nullptr,
        .stopDevice = nullptr,
        .api = nullptr,
        .deviceType = nullptr,
        .internal = { 0 }
    };

    Driver* found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, nullptr);

    CHECK_EQ(driver_construct(&driver), ERROR_NONE);

    found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, &driver);

    CHECK_EQ(driver_destruct(&driver), ERROR_NONE);

    found_driver = driver_find_compatible("test_compatible");
    CHECK_EQ(found_driver, nullptr);
}
