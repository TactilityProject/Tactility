#include "doctest.h"
#include <Tactility/FreeRTOS/task.h>
#include <Tactility/concurrent/Dispatcher.h>

TEST_CASE("dispatcher test") {
    DispatcherHandle_t dispatcher = dispatcher_alloc();
    CHECK_NE(dispatcher, nullptr);

    int count = 0;
    auto error = dispatcher_dispatch(dispatcher, &count, [](void* context) {
        int* count_ptr = static_cast<int*>(context);
        (*count_ptr)++;
    });

    CHECK_EQ(error, ERROR_NONE);
    vTaskDelay(1);

    CHECK_EQ(count, 0);

    CHECK_EQ(dispatcher_consume(dispatcher), ERROR_NONE);
    CHECK_EQ(count, 1);

    dispatcher_free(dispatcher);
}

