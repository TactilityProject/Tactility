#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <cassert>

#include "FreeRTOS.h"
#include "task.h"

struct TestTaskData {
    int argc;
    char** argv;
    int result;
};

void test_task(void* parameter) {
    auto* data = static_cast<TestTaskData*>(parameter);

    doctest::Context context;
    context.applyCommandLine(data->argc, data->argv);
    context.setOption("no-breaks", true);
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

    BaseType_t task_result = xTaskCreate(test_task, "test_task", 8192, &data, 1, nullptr);
    assert(task_result == pdPASS);

    vTaskStartScheduler();
    return data.result;
}
