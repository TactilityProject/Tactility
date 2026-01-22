#pragma once

#include "FreeRTOS.h"

#ifdef ESP_PLATFORM
#include <freertos/semphr.h>
#else
#include <semphr.h>
#endif