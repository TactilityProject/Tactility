#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include <cassert>

#include "FreeRTOS.h"
#include "task.h"

#include <tactility/kernel_init.h>
#include <tactility/hal_device_module.h>

typedef struct {
    int argc;
    char** argv;
    int result;
} TestTaskData;

// From the relevant platform
extern "C" struct Module platform_posix_module;

void test_task(void* parameter) {
    auto* data = (TestTaskData*)parameter;

    doctest::Context context;

    context.applyCommandLine(data->argc, data->argv);

    // overrides
    context.setOption("no-breaks", true); // don't break in the debugger when assertions fail

    Module* dts_modules[] = { &platform_posix_module, nullptr };
    check(kernel_init(&hal_device_module, dts_modules, nullptr) == ERROR_NONE);

    data->result = context.run();

    vTaskEndScheduler();

    vTaskDelete(nullptr);
}

int main(int argc, char** argv) {
    TestTaskData data = {
        .argc = argc,
        .argv = argv,
        .result = 0
    };

    BaseType_t task_result = xTaskCreate(
        test_task,
        "test_task",
        8192,
        &data,
        1,
        nullptr
    );
    assert(task_result == pdPASS);

    vTaskStartScheduler();

    return data.result;
}
