// SPDX-License-Identifier: Apache-2.0
#include "freertos_task_hooks.h"

static FreeRtosTaskCreateHook create_hook = NULL;
static FreeRtosTaskDeleteHook delete_hook = NULL;

void freertos_set_task_hooks(FreeRtosTaskCreateHook create, FreeRtosTaskDeleteHook delete_task) {
    create_hook = create;
    delete_hook = delete_task;
}

BaseType_t xTaskCreate(TaskFunction_t function, const char* const name, const configSTACK_DEPTH_TYPE stack_depth, void* const parameter, UBaseType_t priority, TaskHandle_t* const out_handle) {
    if (create_hook != NULL) {
        return create_hook(__builtin_return_address(0), function, name, stack_depth, parameter, priority, out_handle);
    }
    return freertos_real_xTaskCreate(function, name, stack_depth, parameter, priority, out_handle);
}

void vTaskDelete(TaskHandle_t handle) {
    if (delete_hook != NULL) {
        delete_hook(__builtin_return_address(0), handle);
        return;
    }
    freertos_real_vTaskDelete(handle);
}
