#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver esp32_gpio_driver;
extern Driver esp32_i2c_driver;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct(&esp32_gpio_driver) == ERROR_NONE);
    check(driver_construct(&esp32_i2c_driver) == ERROR_NONE);
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_destruct(&esp32_gpio_driver) == ERROR_NONE);
    check(driver_destruct(&esp32_i2c_driver) == ERROR_NONE);
    return ERROR_NONE;
}

// The name must be exactly "platform_module"
struct Module platform_module = {
    .name = "ESP32 Platform",
    .start = start,
    .stop = stop
};

}
