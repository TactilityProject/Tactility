// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FreeRTOS.h"

#ifndef ESP_PLATFORM
#define xPortInIsrContext(x) (false)
#define vPortAssertIfInISR()
#endif
