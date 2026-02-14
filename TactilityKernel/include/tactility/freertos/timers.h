// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "freertos.h"

#ifdef ESP_PLATFORM
#include <freertos/timers.h>
#else
#include <timers.h>
#endif
