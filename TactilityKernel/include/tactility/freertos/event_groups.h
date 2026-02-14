// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "freertos.h"

#ifdef ESP_PLATFORM
#include <freertos/event_groups.h>
#else
#include <event_groups.h>
#endif

