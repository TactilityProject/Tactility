#include <private/elf_symbol.h>
#include <cstddef>

#include <symbols/freertos.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>

const esp_elfsym freertos_symbols[] = {
    // Task
    ESP_ELFSYM_EXPORT(uxTaskGetStackHighWaterMark),
    ESP_ELFSYM_EXPORT(uxTaskGetNumberOfTasks),
    ESP_ELFSYM_EXPORT(uxTaskGetTaskNumber),
    ESP_ELFSYM_EXPORT(uxTaskPriorityGet),
    ESP_ELFSYM_EXPORT(uxTaskPriorityGetFromISR),
    ESP_ELFSYM_EXPORT(vTaskDelay),
    ESP_ELFSYM_EXPORT(vTaskDelete),
    ESP_ELFSYM_EXPORT(vTaskDeleteWithCaps),
    ESP_ELFSYM_EXPORT(vTaskSetTimeOutState),
    ESP_ELFSYM_EXPORT(vTaskPrioritySet),
    ESP_ELFSYM_EXPORT(vTaskSetTaskNumber),
    ESP_ELFSYM_EXPORT(vTaskSetThreadLocalStoragePointer),
    ESP_ELFSYM_EXPORT(vTaskSetThreadLocalStoragePointerAndDelCallback),
    ESP_ELFSYM_EXPORT(vTaskGetInfo),
    ESP_ELFSYM_EXPORT(vTaskResume),
    ESP_ELFSYM_EXPORT(vTaskSuspend),
    ESP_ELFSYM_EXPORT(xTaskCreate),
    ESP_ELFSYM_EXPORT(xTaskAbortDelay),
    ESP_ELFSYM_EXPORT(xTaskCheckForTimeOut),
    ESP_ELFSYM_EXPORT(xTaskCreatePinnedToCore),
    ESP_ELFSYM_EXPORT(xTaskCreateStatic),
    ESP_ELFSYM_EXPORT(xTaskCreateStaticPinnedToCore),
    ESP_ELFSYM_EXPORT(xTaskCreateWithCaps),
    ESP_ELFSYM_EXPORT(xTaskDelayUntil),
    ESP_ELFSYM_EXPORT(xTaskGenericNotify),
    ESP_ELFSYM_EXPORT(xTaskGenericNotifyFromISR),
    ESP_ELFSYM_EXPORT(xTaskGetTickCount),
    ESP_ELFSYM_EXPORT(xTaskGetTickCountFromISR),
    ESP_ELFSYM_EXPORT(pvTaskGetThreadLocalStoragePointer),
    ESP_ELFSYM_EXPORT(pvTaskIncrementMutexHeldCount),
    // EventGroup
    ESP_ELFSYM_EXPORT(xEventGroupCreate),
    ESP_ELFSYM_EXPORT(xEventGroupCreateWithCaps),
    ESP_ELFSYM_EXPORT(xEventGroupCreateStatic),
    ESP_ELFSYM_EXPORT(xEventGroupClearBits),
    ESP_ELFSYM_EXPORT(xEventGroupClearBitsFromISR),
    ESP_ELFSYM_EXPORT(vEventGroupDelete),
    ESP_ELFSYM_EXPORT(xEventGroupGetStaticBuffer),
    ESP_ELFSYM_EXPORT(xEventGroupGetBitsFromISR),
    ESP_ELFSYM_EXPORT(xEventGroupSetBits),
    ESP_ELFSYM_EXPORT(xEventGroupSetBitsFromISR),
    ESP_ELFSYM_EXPORT(xEventGroupSync),
    ESP_ELFSYM_EXPORT(xEventGroupWaitBits),
    // Queue
    ESP_ELFSYM_EXPORT(vQueueDelete),
    ESP_ELFSYM_EXPORT(vQueueDeleteWithCaps),
    ESP_ELFSYM_EXPORT(vQueueSetQueueNumber),
    ESP_ELFSYM_EXPORT(vQueueWaitForMessageRestricted),
    ESP_ELFSYM_EXPORT(uxQueueGetQueueNumber),
    ESP_ELFSYM_EXPORT(uxQueueMessagesWaiting),
    ESP_ELFSYM_EXPORT(uxQueueMessagesWaitingFromISR),
    ESP_ELFSYM_EXPORT(uxQueueSpacesAvailable),
    ESP_ELFSYM_EXPORT(xQueueCreateCountingSemaphore),
    ESP_ELFSYM_EXPORT(xQueueCreateCountingSemaphoreStatic),
    ESP_ELFSYM_EXPORT(xQueueCreateMutex),
    ESP_ELFSYM_EXPORT(xQueueCreateMutexStatic),
    ESP_ELFSYM_EXPORT(xQueueCreateSet),
    ESP_ELFSYM_EXPORT(xQueueGetMutexHolder),
    ESP_ELFSYM_EXPORT(xQueueGetMutexHolderFromISR),
    ESP_ELFSYM_EXPORT(xQueueGiveMutexRecursive),
    ESP_ELFSYM_EXPORT(xQueueTakeMutexRecursive),
    ESP_ELFSYM_EXPORT(xQueueGenericCreate),
    ESP_ELFSYM_EXPORT(xQueueGenericCreateStatic),
    ESP_ELFSYM_EXPORT(xQueueGenericReset),
    ESP_ELFSYM_EXPORT(xQueueGenericSend),
    ESP_ELFSYM_EXPORT(xQueueGenericSendFromISR),
    ESP_ELFSYM_EXPORT(xQueueSemaphoreTake),
    ESP_ELFSYM_EXPORT(xQueueReceive),
    // Timer
    ESP_ELFSYM_EXPORT(pvTimerGetTimerID),
    ESP_ELFSYM_EXPORT(xTimerCreate),
    ESP_ELFSYM_EXPORT(xTimerCreateStatic),
    ESP_ELFSYM_EXPORT(xTimerGenericCommand),
    ESP_ELFSYM_EXPORT(xTimerIsTimerActive),
    ESP_ELFSYM_EXPORT(xTimerGetExpiryTime),
    ESP_ELFSYM_EXPORT(xTimerPendFunctionCall),
    ESP_ELFSYM_EXPORT(xTimerPendFunctionCallFromISR),
    ESP_ELFSYM_EXPORT(xTimerGetPeriod),
    ESP_ELFSYM_EXPORT(uxTimerGetReloadMode),
    ESP_ELFSYM_EXPORT(uxTimerGetTimerNumber),
    ESP_ELFSYM_EXPORT(vTimerSetReloadMode),
    ESP_ELFSYM_EXPORT(vTimerSetTimerID),
    ESP_ELFSYM_EXPORT(vTimerSetTimerNumber),
    // portmacro.h
    ESP_ELFSYM_EXPORT(vPortYield),
    ESP_ELFSYM_EXPORT(vPortEnterCritical),
    ESP_ELFSYM_EXPORT(vPortExitCritical),
    ESP_ELFSYM_EXPORT(xPortInIsrContext),
    ESP_ELFSYM_EXPORT(xPortCanYield),
    ESP_ELFSYM_EXPORT(xPortGetCoreID),
    ESP_ELFSYM_EXPORT(xPortGetTickRateHz),
    ESP_ELFSYM_EXPORT(xPortInterruptedFromISRContext),
    // delimiter
    ESP_ELFSYM_END
};
