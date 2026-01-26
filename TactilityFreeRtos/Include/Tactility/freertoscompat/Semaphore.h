// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <FreeRTOS.h>
#include <semphr.h>
#endif

#include <cassert>

struct SemaphoreHandleDeleter {
  static void operator()(QueueHandle_t handleToDelete) {
    assert(xPortInIsrContext() == pdFALSE);
    vSemaphoreDelete(handleToDelete);
  }
};

