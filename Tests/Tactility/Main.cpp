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

extern "C" {
// From the relevant platform
extern struct Module platform_module;
// From the relevant device
extern struct Module device_module;
}

struct ModuleParent tactility_tests_module_parent  {
    "tactility-tests",
    nullptr
};

void test_task(void* parameter) {
    auto* data = (TestTaskData*)parameter;

    doctest::Context context;

    context.applyCommandLine(data->argc, data->argv);

    // overrides
    context.setOption("no-breaks", true); // don't break in the debugger when assertions fail

    check(kernel_init(&platform_module, &device_module, nullptr) == ERROR_NONE);
    // HAL compatibility module: it creates kernel driver wrappers for tt::hal::Device
    check(module_parent_construct(&tactility_tests_module_parent) == ERROR_NONE);
    check(module_set_parent(&hal_device_module, &tactility_tests_module_parent) == ERROR_NONE);
    check(module_start(&hal_device_module) == ERROR_NONE);

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
