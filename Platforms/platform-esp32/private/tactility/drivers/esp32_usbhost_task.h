#pragma once

#include <sdkconfig.h>

#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <esp_heap_caps.h>

/**
 * Creates a task with its stack in PSRAM when available, otherwise in internal RAM.
 * The task must be deleted with vTaskDeleteWithCaps().
 * The task must never perform SPI flash operations, because these crash when the stack is in PSRAM.
 */
inline BaseType_t esp32_usbhost_task_create_psram(TaskFunction_t function, const char* name, uint32_t stack_size, void* arg, UBaseType_t priority, TaskHandle_t* handle) {
#if CONFIG_SPIRAM
    if (xTaskCreateWithCaps(function, name, stack_size, arg, priority, handle, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return pdPASS;
    }
#endif
    return xTaskCreateWithCaps(function, name, stack_size, arg, priority, handle, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
