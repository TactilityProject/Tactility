// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "RTOS.h"

#ifndef ESP_PLATFORM

// TactilityKernel co-existence check
#ifndef xPortInIsrContext
#define xPortInIsrContext(x) (false)
#endif

#endif
