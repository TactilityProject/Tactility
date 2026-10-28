#include "doctest.h"

#include <cmath>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/compass.h>

namespace {

CompassData mock_field = {};
error_t mock_field_error = ERROR_NONE;
float mock_heading = 0.0f;

int start(Device*) { return ERROR_NONE; }
int stop(Device*) { return ERROR_NONE; }

error_t read_field(Device*, CompassData* data) {
    if (mock_field_error != ERROR_NONE) return mock_field_error;
    *data = mock_field;
    return ERROR_NONE;
}

error_t read_heading(Device*, float* heading_deg) {
    *heading_deg = mock_heading;
    return ERROR_NONE;
}

const CompassApi derived_api = { .read_field = read_field, .read_heading = nullptr };
const CompassApi native_api = { .read_field = read_field, .read_heading = read_heading };

Driver derived_driver = {
    .name = "compass_test_derived",
    .compatible = (const char*[]) { "compass_test_derived", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &derived_api,
    .device_type = &COMPASS_TYPE,
    .internal = nullptr,
};

Driver native_driver = {
    .name = "compass_test_native",
    .compatible = (const char*[]) { "compass_test_native", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &native_api,
    .device_type = &COMPASS_TYPE,
    .internal = nullptr,
};

/** Runs a body with a device bound to the driver */
template <typename Body>
void with_device(Driver* driver, Body body) {
    Device device = { .name = "compass_test_device", .config = nullptr, .parent = nullptr };
    REQUIRE_EQ(driver_construct_add(driver), ERROR_NONE);
    REQUIRE_EQ(device_construct(&device), ERROR_NONE);
    device_set_driver(&device, driver);
    REQUIRE_EQ(device_add(&device), ERROR_NONE);

    body(&device);

    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(driver), ERROR_NONE);
}

float derived_heading(float x, float y) {
    mock_field = { x, y, 0.0f };
    float heading = -1.0f;
    with_device(&derived_driver, [&](Device* device) {
        CHECK_EQ(compass_read_heading(device, &heading), ERROR_NONE);
    });
    return heading;
}

} // namespace

TEST_CASE("compass_read_heading derives the cardinal directions from the field") {
    mock_field_error = ERROR_NONE;
    CHECK(derived_heading(1.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(derived_heading(0.0f, 1.0f) == doctest::Approx(90.0f));
    CHECK(derived_heading(-1.0f, 0.0f) == doctest::Approx(180.0f));
    CHECK(derived_heading(0.0f, -1.0f) == doctest::Approx(270.0f));
}

TEST_CASE("compass_read_heading derives headings in the range [0, 360)") {
    mock_field_error = ERROR_NONE;
    CHECK(derived_heading(1.0f, 1.0f) == doctest::Approx(45.0f));
    CHECK(derived_heading(1.0f, -1.0f) == doctest::Approx(315.0f));
    CHECK(derived_heading(-1.0f, -1.0f) == doctest::Approx(225.0f));

    // Slightly below the x axis must wrap to just under 360, not go negative
    const float heading = derived_heading(1.0f, -0.001f);
    CHECK(heading >= 0.0f);
    CHECK(heading < 360.0f);
    CHECK(heading == doctest::Approx(359.94f).epsilon(0.001));
}

TEST_CASE("compass_read_heading ignores field strength and the z axis") {
    mock_field_error = ERROR_NONE;
    CHECK(derived_heading(40.0f, 40.0f) == doctest::Approx(45.0f));

    mock_field = { 1.0f, 0.0f, 500.0f };
    float heading = -1.0f;
    with_device(&derived_driver, [&](Device* device) {
        CHECK_EQ(compass_read_heading(device, &heading), ERROR_NONE);
    });
    CHECK(heading == doctest::Approx(0.0f));
}

TEST_CASE("compass_read_heading propagates the field read error and leaves the output untouched") {
    mock_field_error = ERROR_TIMEOUT;
    float heading = 123.0f;
    with_device(&derived_driver, [&](Device* device) {
        CHECK_EQ(compass_read_heading(device, &heading), ERROR_TIMEOUT);
    });
    CHECK_EQ(heading, 123.0f);
    mock_field_error = ERROR_NONE;
}

TEST_CASE("compass_read_heading prefers the driver's own heading") {
    mock_field_error = ERROR_NONE;
    mock_field = { 1.0f, 0.0f, 0.0f };
    mock_heading = 200.0f;
    float heading = -1.0f;
    with_device(&native_driver, [&](Device* device) {
        CHECK_EQ(compass_read_heading(device, &heading), ERROR_NONE);
    });
    CHECK(heading == doctest::Approx(200.0f));
}

TEST_CASE("compass_read_field returns the driver's reading") {
    mock_field_error = ERROR_NONE;
    mock_field = { 1.5f, -2.5f, 3.5f };
    CompassData data = {};
    with_device(&derived_driver, [&](Device* device) {
        CHECK_EQ(compass_read_field(device, &data), ERROR_NONE);
    });
    CHECK(data.x == doctest::Approx(1.5f));
    CHECK(data.y == doctest::Approx(-2.5f));
    CHECK(data.z == doctest::Approx(3.5f));
}
