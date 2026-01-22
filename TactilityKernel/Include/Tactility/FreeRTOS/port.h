#pragma once

#include "FreeRTOS.h"

#ifndef ESP_PLATFORM
#define xPortInIsrContext(x) (false)
#define vPortAssertIfInISR()
#endif
