// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "RTOS.h"

#ifndef ESP_PLATFORM
#define xPortInIsrContext(x) (false)
#endif
