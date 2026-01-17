#pragma once

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#else
#include <FreeRTOS.h>
#include <timers.h>
#endif
