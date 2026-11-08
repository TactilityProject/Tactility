// SPDX-License-Identifier: Apache-2.0
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <freertos/module.h>

#include <tactility/freertos/event_groups.h>
#include <tactility/freertos/queue.h>
#include <tactility/freertos/task.h>
#include <tactility/freertos/timers.h>

// Deprecated app_execute*/app_start* symbols stay exported for apps built against older SDKs
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

extern "C" {

static const ModuleSymbol SYMBOLS[] = {
    // Task
    DEFINE_MODULE_SYMBOL(uxTaskGetStackHighWaterMark),
    DEFINE_MODULE_SYMBOL(uxTaskGetNumberOfTasks),
    DEFINE_MODULE_SYMBOL(uxTaskGetTaskNumber),
    DEFINE_MODULE_SYMBOL(uxTaskPriorityGet),
    DEFINE_MODULE_SYMBOL(uxTaskPriorityGetFromISR),
    DEFINE_MODULE_SYMBOL(vTaskDelay),
    DEFINE_MODULE_SYMBOL(vTaskDelete),
#ifdef ESP_PLATFORM
    // ESP-IDF FreeRTOS extension (memory-capability-aware alloc); not in vanilla FreeRTOS-Kernel.
    DEFINE_MODULE_SYMBOL(vTaskDeleteWithCaps),
#endif
    DEFINE_MODULE_SYMBOL(vTaskSetTimeOutState),
    DEFINE_MODULE_SYMBOL(vTaskPrioritySet),
    DEFINE_MODULE_SYMBOL(vTaskSetTaskNumber),
    DEFINE_MODULE_SYMBOL(vTaskSetThreadLocalStoragePointer),
#ifdef ESP_PLATFORM
    // ESP-IDF FreeRTOS extension (TLS pointer with destructor callback, used by pthread emulation).
    DEFINE_MODULE_SYMBOL(vTaskSetThreadLocalStoragePointerAndDelCallback),
#endif
    DEFINE_MODULE_SYMBOL(vTaskGetInfo),
    DEFINE_MODULE_SYMBOL(vTaskResume),
    DEFINE_MODULE_SYMBOL(vTaskSuspend),
    DEFINE_MODULE_SYMBOL(xTaskCreate),
    DEFINE_MODULE_SYMBOL(xTaskAbortDelay),
    DEFINE_MODULE_SYMBOL(xTaskCheckForTimeOut),
#ifdef ESP_PLATFORM
    // Xtensa_ESP32 port only; the POSIX port has no multi-core pinning.
    DEFINE_MODULE_SYMBOL(xTaskCreatePinnedToCore),
#endif
#if configSUPPORT_STATIC_ALLOCATION == 1
    DEFINE_MODULE_SYMBOL(xTaskCreateStatic),
#ifdef ESP_PLATFORM
    DEFINE_MODULE_SYMBOL(xTaskCreateStaticPinnedToCore),
#endif
#endif
#ifdef ESP_PLATFORM
    // ESP-IDF FreeRTOS extensions (memory-capability-aware alloc); not in vanilla FreeRTOS-Kernel.
    DEFINE_MODULE_SYMBOL(xTaskCreateWithCaps),
    DEFINE_MODULE_SYMBOL(xTaskCreatePinnedToCoreWithCaps),
#endif
    DEFINE_MODULE_SYMBOL(xTaskDelayUntil),
    DEFINE_MODULE_SYMBOL(xTaskGenericNotify),
    DEFINE_MODULE_SYMBOL(xTaskGenericNotifyFromISR),
    DEFINE_MODULE_SYMBOL(ulTaskGenericNotifyTake),
    DEFINE_MODULE_SYMBOL(xTaskGetCurrentTaskHandle),
    DEFINE_MODULE_SYMBOL(xTaskGetTickCount),
    DEFINE_MODULE_SYMBOL(xTaskGetTickCountFromISR),
    DEFINE_MODULE_SYMBOL(pvTaskGetThreadLocalStoragePointer),
    DEFINE_MODULE_SYMBOL(pvTaskIncrementMutexHeldCount),
    // EventGroup
    DEFINE_MODULE_SYMBOL(xEventGroupCreate),
#ifdef ESP_PLATFORM
    // ESP-IDF FreeRTOS extension (memory-capability-aware alloc); not in vanilla FreeRTOS-Kernel.
    DEFINE_MODULE_SYMBOL(xEventGroupCreateWithCaps),
#endif
#if configSUPPORT_STATIC_ALLOCATION == 1
    DEFINE_MODULE_SYMBOL(xEventGroupCreateStatic),
    DEFINE_MODULE_SYMBOL(xEventGroupGetStaticBuffer),
#endif
    DEFINE_MODULE_SYMBOL(xEventGroupClearBits),
    DEFINE_MODULE_SYMBOL(xEventGroupClearBitsFromISR),
    DEFINE_MODULE_SYMBOL(vEventGroupDelete),
    DEFINE_MODULE_SYMBOL(xEventGroupGetBitsFromISR),
    DEFINE_MODULE_SYMBOL(xEventGroupSetBits),
    DEFINE_MODULE_SYMBOL(xEventGroupSetBitsFromISR),
    DEFINE_MODULE_SYMBOL(xEventGroupSync),
    DEFINE_MODULE_SYMBOL(xEventGroupWaitBits),
    // Queue
    DEFINE_MODULE_SYMBOL(vQueueDelete),
#ifdef ESP_PLATFORM
    // ESP-IDF FreeRTOS extension (memory-capability-aware alloc); not in vanilla FreeRTOS-Kernel.
    DEFINE_MODULE_SYMBOL(vQueueDeleteWithCaps),
#endif
    DEFINE_MODULE_SYMBOL(vQueueSetQueueNumber),
    DEFINE_MODULE_SYMBOL(vQueueWaitForMessageRestricted),
    DEFINE_MODULE_SYMBOL(uxQueueGetQueueNumber),
    DEFINE_MODULE_SYMBOL(uxQueueMessagesWaiting),
    DEFINE_MODULE_SYMBOL(uxQueueMessagesWaitingFromISR),
    DEFINE_MODULE_SYMBOL(uxQueueSpacesAvailable),
    DEFINE_MODULE_SYMBOL(xQueueCreateCountingSemaphore),
#if configSUPPORT_STATIC_ALLOCATION == 1
    DEFINE_MODULE_SYMBOL(xQueueCreateCountingSemaphoreStatic),
    DEFINE_MODULE_SYMBOL(xQueueCreateMutexStatic),
    DEFINE_MODULE_SYMBOL(xQueueGenericCreateStatic),
#endif
    DEFINE_MODULE_SYMBOL(xQueueCreateMutex),
    DEFINE_MODULE_SYMBOL(xQueueCreateSet),
    DEFINE_MODULE_SYMBOL(xQueueGetMutexHolder),
    DEFINE_MODULE_SYMBOL(xQueueGetMutexHolderFromISR),
    DEFINE_MODULE_SYMBOL(xQueueGiveMutexRecursive),
    DEFINE_MODULE_SYMBOL(xQueueTakeMutexRecursive),
    DEFINE_MODULE_SYMBOL(xQueueGenericCreate),
    DEFINE_MODULE_SYMBOL(xQueueGenericReset),
    DEFINE_MODULE_SYMBOL(xQueueGenericSend),
    DEFINE_MODULE_SYMBOL(xQueueGenericSendFromISR),
    DEFINE_MODULE_SYMBOL(xQueueSemaphoreTake),
    DEFINE_MODULE_SYMBOL(xQueueReceive),
    // Timer
    DEFINE_MODULE_SYMBOL(pvTimerGetTimerID),
    DEFINE_MODULE_SYMBOL(xTimerCreate),
#if configSUPPORT_STATIC_ALLOCATION == 1
    DEFINE_MODULE_SYMBOL(xTimerCreateStatic),
#endif
#if tskKERNEL_VERSION_MAJOR >= 11
    // A macro over these two functions since FreeRTOS 11
    DEFINE_MODULE_SYMBOL(xTimerGenericCommandFromTask),
    DEFINE_MODULE_SYMBOL(xTimerGenericCommandFromISR),
#else
    DEFINE_MODULE_SYMBOL(xTimerGenericCommand),
#endif
    DEFINE_MODULE_SYMBOL(xTimerIsTimerActive),
    DEFINE_MODULE_SYMBOL(xTimerGetExpiryTime),
    DEFINE_MODULE_SYMBOL(xTimerPendFunctionCall),
    DEFINE_MODULE_SYMBOL(xTimerPendFunctionCallFromISR),
    DEFINE_MODULE_SYMBOL(xTimerGetPeriod),
    DEFINE_MODULE_SYMBOL(uxTimerGetReloadMode),
    DEFINE_MODULE_SYMBOL(uxTimerGetTimerNumber),
    DEFINE_MODULE_SYMBOL(vTimerSetReloadMode),
    DEFINE_MODULE_SYMBOL(vTimerSetTimerID),
    DEFINE_MODULE_SYMBOL(vTimerSetTimerNumber),
    // portmacro.h
    DEFINE_MODULE_SYMBOL(vPortYield),
    DEFINE_MODULE_SYMBOL(vPortEnterCritical),
    DEFINE_MODULE_SYMBOL(vPortExitCritical),
#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32S3)
    DEFINE_MODULE_SYMBOL(xPortEnterCriticalTimeout),
#endif
#if (configNUM_CORES > 1)
    DEFINE_MODULE_SYMBOL(vPortExitCriticalCompliance),
#endif
#ifdef CONFIG_IDF_TARGET_ESP32P4
    DEFINE_MODULE_SYMBOL(vPortExitCriticalMultiCore),
#endif
#ifdef ESP_PLATFORM
    // freertos_tasks_c_additions.h - Xtensa_ESP32 port / ESP-IDF newlib integration only.
    DEFINE_MODULE_SYMBOL(xPortInIsrContext),
    DEFINE_MODULE_SYMBOL(xPortCanYield),
    DEFINE_MODULE_SYMBOL(xPortGetCoreID),
    DEFINE_MODULE_SYMBOL(xPortGetTickRateHz),
    DEFINE_MODULE_SYMBOL(xPortInterruptedFromISRContext),
    DEFINE_MODULE_SYMBOL(__getreent),
#endif
    MODULE_SYMBOL_TERMINATOR,
};

Module freertos_module = {
    .name = "freertos",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}
