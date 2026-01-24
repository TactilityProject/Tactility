#include "doctest.h"
#include <Tactility/FreeRTOS/task.h>
#include <Tactility/concurrent/Dispatcher.h>

TEST_CASE("dispatcher test") {
    DispatcherHandle_t dispatcher = dispatcher_alloc();
    CHECK_NE(dispatcher, nullptr);

    int count = 0;
    bool dispatch_success = dispatcher_dispatch(dispatcher, &count, [](void* context) {
        int* count_ptr = static_cast<int*>(context);
        (*count_ptr)++;
    });

    CHECK_EQ(dispatch_success, true);
    vTaskDelay(1);

    CHECK_EQ(count, 0);

    int consume_count = dispatcher_consume(dispatcher);
    CHECK_EQ(consume_count, 1);
    CHECK_EQ(count, 1);

    dispatcher_free(dispatcher);
}

