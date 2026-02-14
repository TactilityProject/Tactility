// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#else
#include <FreeRTOS.h>
#endif

// Custom port compatibility definitins, mainly for PC compatibility
#include "port.h"
