#pragma once

#include "FreeRTOS.h"

#ifdef ESP_PLATFORM
#include <freertos/event_groups.h>
#else
#include <event_groups.h>
#endif

