#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include <tactility/check.h>

#include <tactility/dts.h>
#include <tactility/freertos/task.h>
#include <tactility/kernel_init.h>

#include <mock_drivers_module.h>

typedef struct {
    int argc;
    char** argv;
    int result;
} TestTaskData;

extern "C" struct Module platform_posix_module;

void test_task(void* parameter) {
    auto* data = (TestTaskData*)parameter;

    doctest::Context context;
    context.applyCommandLine(data->argc, data->argv);
    context.setOption("no-breaks", true);

    Module* dts_modules[] = { &platform_posix_module, &mock_drivers_module, nullptr };
    DtsDevice dts_devices[] = { DTS_DEVICE_TERMINATOR };
    check(kernel_init(dts_modules, dts_devices) == ERROR_NONE);

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

    if (xTaskCreate(test_task, "test_task", 16384, &data, 1, nullptr) != pdPASS) {
        return 1;
    }

    vTaskStartScheduler();
    return data.result;
}
