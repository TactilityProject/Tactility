#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/esp32_led_strip.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_led_strip, struct LedStripConfig)

#ifdef __cplusplus
}
#endif
