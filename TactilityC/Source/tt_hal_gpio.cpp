#include "tt_hal_gpio.h"
#include <Tactility/hal/gpio/Gpio.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>

extern "C" {

using namespace tt::hal;

bool tt_hal_gpio_get_level(GpioPin pin) {
    return false;
}

int tt_hal_gpio_get_pin_count() {
    return 0;
}

}
