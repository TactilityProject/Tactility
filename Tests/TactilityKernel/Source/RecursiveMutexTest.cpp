#include "doctest.h"
#include <tactility/concurrent/recursive_mutex.h>

TEST_CASE("recursive_mutex_construct and mutex_destruct should properly set the handle") {
    RecursiveMutex mutex = { 0 };
    recursive_mutex_construct(&mutex);
    CHECK_NE(mutex.handle, nullptr);
    recursive_mutex_destruct(&mutex);
    CHECK_EQ(mutex.handle, nullptr);
}

TEST_CASE("recursive_mutex_is_locked should return true only when the mutex is locked") {
    RecursiveMutex mutex = { 0 };
    recursive_mutex_construct(&mutex);

    CHECK_EQ(recursive_mutex_is_locked(&mutex), false);
    recursive_mutex_lock(&mutex);
    CHECK_EQ(recursive_mutex_is_locked(&mutex), true);
    recursive_mutex_unlock(&mutex);
    CHECK_EQ(recursive_mutex_is_locked(&mutex), false);

    recursive_mutex_destruct(&mutex);
}

TEST_CASE("recursive_mutex_is_locked can lock twice from the same thread") {
    RecursiveMutex mutex = { 0 };
    recursive_mutex_construct(&mutex);

    CHECK_EQ(recursive_mutex_is_locked(&mutex), false);
    recursive_mutex_lock(&mutex);
    CHECK_EQ(recursive_mutex_is_locked(&mutex), true);
    recursive_mutex_lock(&mutex);
    CHECK_EQ(recursive_mutex_is_locked(&mutex), true);
    recursive_mutex_unlock(&mutex);
    CHECK_EQ(recursive_mutex_is_locked(&mutex), true);
    recursive_mutex_unlock(&mutex);
    CHECK_EQ(recursive_mutex_is_locked(&mutex), false);

    recursive_mutex_destruct(&mutex);
}

TEST_CASE("recursive_mutex_try_lock should lock multiple times from the same thread") {
    RecursiveMutex mutex = { 0 };
    recursive_mutex_construct(&mutex);

    CHECK_EQ(recursive_mutex_try_lock(&mutex), true);
    CHECK_EQ(recursive_mutex_try_lock(&mutex), true);
    recursive_mutex_unlock(&mutex);
    CHECK_EQ(recursive_mutex_is_locked(&mutex), true);
    recursive_mutex_unlock(&mutex);
    CHECK_EQ(recursive_mutex_is_locked(&mutex), false);

    recursive_mutex_destruct(&mutex);
}

TEST_CASE("recursive_mutex_lock in another task should block when a lock is active") {
    static int task_lock_counter = 0;
    RecursiveMutex mutex = { 0 };
    task_lock_counter = 0;

    recursive_mutex_construct(&mutex);
    recursive_mutex_lock(&mutex);

    TaskHandle_t task_handle;
    auto task_create_result = xTaskCreate(
        [](void* input) {
            RecursiveMutex* mutex_ptr = static_cast<RecursiveMutex*>(input);
            recursive_mutex_lock(mutex_ptr);
            task_lock_counter++;
            vTaskDelete(nullptr);
        },
        "mutex_test",
        2048,
        &mutex,
        0,
        &task_handle
    );

    CHECK_EQ(task_create_result, pdPASS);
    CHECK_EQ(task_lock_counter, 0);

    recursive_mutex_unlock(&mutex);
    vTaskDelay(2); // 1 is sufficient most of the time, but not always
    CHECK_EQ(task_lock_counter, 1);
    recursive_mutex_destruct(&mutex);
}
