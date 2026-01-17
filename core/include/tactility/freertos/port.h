#pragma once

#include "freertos.h"

#ifndef ESP_PLATFORM
#define xPortInIsrContext(x) (false)
#endif
