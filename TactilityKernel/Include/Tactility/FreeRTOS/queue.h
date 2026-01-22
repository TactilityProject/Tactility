#pragma once

#include "FreeRTOS.h"

#ifdef ESP_PLATFORM
#include <freertos/queue.h>
#else
#include <queue.h>
#endif

