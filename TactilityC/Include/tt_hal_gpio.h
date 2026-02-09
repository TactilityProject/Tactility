#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @deprecated NON-FUNCTIONAL - WILL BE REMOVED SOON */
typedef unsigned int GpioPin;

/** @deprecated NON-FUNCTIONAL - WILL BE REMOVED SOON */
#define GPIO_NO_PIN -1

/** @deprecated NON-FUNCTIONAL - WILL BE REMOVED SOON */
bool tt_hal_gpio_get_level(GpioPin pin);

/** @deprecated NON-FUNCTIONAL - WILL BE REMOVED SOON */
int tt_hal_gpio_get_pin_count();

#ifdef __cplusplus
}
#endif
