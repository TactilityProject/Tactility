// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FreeRTOS.h"

#ifdef ESP_PLATFORM
#include <freertos/task.h>
#else
#include <task.h>
#endif

