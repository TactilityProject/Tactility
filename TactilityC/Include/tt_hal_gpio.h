#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Logical GPIO pin identifier used by the HAL. Typically maps to the SoC GPIO number. */
typedef unsigned int GpioPin;
/** Value indicating that no GPIO pin is used/applicable. */
#define GPIO_NO_PIN -1

/** Read the current logic level of a pin.
 * The pin should be configured for input or input/output.
 * @param[in] pin The pin to read.
 * @return true if the level is high, false if low. If the pin is invalid, the
 *         behavior is implementation-defined and may return false.
 */
bool tt_hal_gpio_get_level(GpioPin pin);

/** Get the number of GPIO pins available on this platform.
 * @return The count of valid GPIO pins.
 */
int tt_hal_gpio_get_pin_count();

#ifdef __cplusplus
}
#endif
