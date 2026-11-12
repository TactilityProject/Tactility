#include "doctest.h"
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/wifi.h>
#include <tactility/network.h>

static WifiStationState fake_station_state = WIFI_STATION_STATE_DISCONNECTED;

static error_t fake_get_station_state(Device*, WifiStationState* state) {
    *state = fake_station_state;
    return ERROR_NONE;
}

static const WifiApi network_test_wifi_api = {
    .get_station_state = fake_get_station_state,
};

static Driver network_test_wifi_driver = {
    .name = "network_test_wifi_driver",
    .compatible = (const char*[]) { "network_test,wifi", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &network_test_wifi_api,
    .device_type = &WIFI_TYPE,
    .internal = nullptr,
};

TEST_CASE("network is only available while a wifi station is connected") {
    static Device device {
        .name = "network_test_wifi_device",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_FALSE(network_is_available());

    CHECK_EQ(driver_construct_add(&network_test_wifi_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&device, "network_test,wifi"), ERROR_NONE);

    // A device that isn't started has no connection
    fake_station_state = WIFI_STATION_STATE_CONNECTED;
    CHECK_FALSE(network_is_available());

    CHECK_EQ(device_start(&device), ERROR_NONE);
    CHECK(network_is_available());

    fake_station_state = WIFI_STATION_STATE_CONNECTION_PENDING;
    CHECK_FALSE(network_is_available());
    fake_station_state = WIFI_STATION_STATE_DISCONNECTED;
    CHECK_FALSE(network_is_available());

    CHECK_EQ(device_stop(&device), ERROR_NONE);
    CHECK_EQ(device_remove(&device), ERROR_NONE);
    CHECK_EQ(device_destruct(&device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&network_test_wifi_driver), ERROR_NONE);
}
