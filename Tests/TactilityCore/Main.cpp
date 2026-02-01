#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include <cassert>

#include "FreeRTOS.h"
#include "task.h"

typedef struct {
    int argc;
    char** argv;
    int result;
} TestTaskData;

void test_task(void* parameter) {
    auto* data = (TestTaskData*)parameter;

    doctest::Context context;

    context.applyCommandLine(data->argc, data->argv);

    // overrides
    context.setOption("no-breaks", true); // don't break in the debugger when assertions fail

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

// NOTE: This is normally provided by the platform kernel module, but that's not loaded for TactilityCore
extern "C" {
// Required for FreeRTOS
void vAssertCalled(unsigned long line, const char* const file) {
    __assert_fail("assert failed", file, line, "");
}
}
