#include "doctest.h"
#include <Tactility/Device.h>
#include <Tactility/Driver.h>

struct IntegrationDriverConfig {
    int startResult;
    int stopResult;
};

static int startCalled = 0;
static int stopCalled = 0;

#define integration_data(device) static_cast<IntegrationDriverData*>(device_get_driver_data(device))
#define integration_config(device) static_cast<const IntegrationDriverConfig*>(device->config)

static int start(Device* device) {
    startCalled++;
    return integration_config(device)->startResult;
}

static int stop(Device* device) {
    stopCalled++;
    return integration_config(device)->stopResult;
}

static Driver integration_driver = {
    .name = "integration_test_driver",
    .compatible = (const char*[]) { "integration", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .internal = { 0 }
};

TEST_CASE("driver with with start success and stop success should start and stop a device") {
    startCalled = 0;
    stopCalled = 0;
    static const IntegrationDriverConfig config {
        .startResult = 0,
        .stopResult = 0
    };

    static Device integration_device {
        .name = "integration_device",
        .config = &config,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct(&integration_driver), 0);

    CHECK_EQ(device_construct(&integration_device), 0);
    device_add(&integration_device);
    CHECK_EQ(startCalled, 0);
    CHECK_EQ(driver_bind(&integration_driver, &integration_device), 0);
    CHECK_EQ(startCalled, 1);
    CHECK_EQ(stopCalled, 0);
    CHECK_EQ(driver_unbind(&integration_driver, &integration_device), 0);
    CHECK_EQ(stopCalled, 1);
    CHECK_EQ(device_remove(&integration_device), 0);
    CHECK_EQ(device_destruct(&integration_device), 0);

    CHECK_EQ(driver_destruct(&integration_driver), 0);
}
