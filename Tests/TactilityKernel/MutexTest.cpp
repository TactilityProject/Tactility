#include "doctest.h"
#include <Tactility/concurrent/Mutex.h>

TEST_CASE("mutex_construct and mutex_destruct should properly set the handle") {
    Mutex mutex = { 0 };
    mutex_construct(&mutex);
    CHECK_NE(mutex.handle, nullptr);
    mutex_destruct(&mutex);
    CHECK_EQ(mutex.handle, nullptr);
}

TEST_CASE("mutex_is_locked should return true only when the mutex is locked") {
    Mutex mutex = { 0 };
    mutex_construct(&mutex);

    CHECK_EQ(mutex_is_locked(&mutex), false);
    mutex_lock(&mutex);
    CHECK_EQ(mutex_is_locked(&mutex), true);
    mutex_unlock(&mutex);
    CHECK_EQ(mutex_is_locked(&mutex), false);

    mutex_destruct(&mutex);
}

TEST_CASE("mutex_try_lock should succeed on first lock but not on second") {
    Mutex mutex = { 0 };
    mutex_construct(&mutex);

    CHECK_EQ(mutex_try_lock(&mutex), true);
    CHECK_EQ(mutex_try_lock(&mutex), false);
    mutex_unlock(&mutex);

    mutex_destruct(&mutex);
}

TEST_CASE("mutex_lock in another task should block when a lock is active") {
    static int task_lock_counter = 0;
    Mutex mutex = { 0 };
    task_lock_counter = 0;

    mutex_construct(&mutex);
    mutex_lock(&mutex);

    TaskHandle_t task_handle;
    auto task_create_result = xTaskCreate(
        [](void* input) {
            Mutex* mutex_ptr = static_cast<Mutex*>(input);
            mutex_lock(mutex_ptr);
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

    mutex_unlock(&mutex);
    vTaskDelay(1);
    CHECK_EQ(task_lock_counter, 1);
    mutex_destruct(&mutex);
}
