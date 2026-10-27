// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver mock_wifi_driver;
extern Driver mock_radio_driver;
extern Driver mock_bluetooth_driver;
extern Driver mock_power_supply_driver;

static Driver* const mock_drivers[] = {
    &mock_wifi_driver,
    &mock_radio_driver,
    &mock_bluetooth_driver,
    &mock_power_supply_driver,
    nullptr
};

Module mock_drivers_module = {
    .name = "mock-drivers",
    .start = nullptr,
    .stop = nullptr,
    .drivers = mock_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
