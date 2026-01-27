#include "doctest.h"

#include <tactility/delay.h>
#include <tactility/concurrent/thread.h>

TEST_CASE("when a thread is started then its callback should be called") {
    bool has_called = false;
    auto* thread = thread_alloc_full(
        "immediate return task",
        4096,
        [](void* context) {
            auto* has_called_ptr = static_cast<bool*>(context);
            *has_called_ptr = true;
            return 0;
        },
        &has_called,
        -1
    );

    CHECK(!has_called);
    CHECK_EQ(thread_start(thread), ERROR_NONE);
    CHECK_EQ(thread_join(thread, 2, 1), ERROR_NONE);
    thread_free(thread);
    CHECK(has_called);
}

TEST_CASE("a thread can be started and stopped") {
    bool interrupted = false;
    auto* thread = thread_alloc_full(
        "interruptable thread",
        4096,
        [](void* context) {
            auto* interrupted_ptr = static_cast<bool*>(context);
            while (!*interrupted_ptr) {
                delay_millis(1);
            }
            return 0;
        },
        &interrupted,
        -1
    );

    CHECK(thread);
    CHECK_EQ(thread_start(thread), ERROR_NONE);
    interrupted = true;
    CHECK_EQ(thread_join(thread, 2, 1), ERROR_NONE);
    thread_free(thread);
}

TEST_CASE("thread id should only be set at when thread is started") {
    bool interrupted = false;
    auto* thread = thread_alloc_full(
        "interruptable thread",
        4096,
        [](void* context) {
          auto* interrupted_ptr = static_cast<bool*>(context);
          while (!*interrupted_ptr) {
              delay_millis(1);
          }
          return 0;
        },
        &interrupted,
        -1
    );
    CHECK_EQ(thread_get_task_handle(thread), nullptr);
    CHECK_EQ(thread_start(thread), ERROR_NONE);
    CHECK_NE(thread_get_task_handle(thread), nullptr);
    interrupted = true;
    CHECK_EQ(thread_join(thread, 2, 1), ERROR_NONE);
    CHECK_EQ(thread_get_task_handle(thread), nullptr);
    thread_free(thread);
}

TEST_CASE("thread state should be correct") {
    bool interrupted = false;
    auto* thread = thread_alloc_full(
        "interruptable thread",
        4096,
        [](void* context) {
          auto* interrupted_ptr = static_cast<bool*>(context);
          while (!*interrupted_ptr) {
              delay_millis(1);
          }
          return 0;
        },
        &interrupted,
        -1

    );
    CHECK_EQ(thread_get_state(thread), THREAD_STATE_STOPPED);
    thread_start(thread);
    auto state = thread_get_state(thread);
    CHECK((state == THREAD_STATE_STARTING || state == THREAD_STATE_RUNNING));
    interrupted = true;
    CHECK_EQ(thread_join(thread, 10, 1), ERROR_NONE);
    CHECK_EQ(thread_get_state(thread), THREAD_STATE_STOPPED);
    thread_free(thread);
}

TEST_CASE("thread id should only be set at when thread is started") {
    auto* thread = thread_alloc_full(
        "return code",
        4096,
        [](void* context) { return 123; },
        nullptr,
        -1
    );
    CHECK_EQ(thread_start(thread), ERROR_NONE);
    CHECK_EQ(thread_join(thread, 1, 1), ERROR_NONE);
    CHECK_EQ(thread_get_return_code(thread), 123);
    thread_free(thread);
}
