// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>

extern "C" {

extern void register_platform_drivers() {
    extern Driver esp32_gpio_driver;
    driver_construct(&esp32_gpio_driver);
    extern Driver esp32_i2c_driver;
    driver_construct(&esp32_i2c_driver);
}

}
