// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "freertos.h"

#ifndef ESP_PLATFORM

// TactilityFreeRTGOS co-existence check
#ifndef xPortInIsrContext
#define xPortInIsrContext() (pdFALSE)
#endif

// TactilityFreeRTGOS co-existence check
#ifndef vPortAssertIfInISR
#define vPortAssertIfInISR()
#endif

#endif
