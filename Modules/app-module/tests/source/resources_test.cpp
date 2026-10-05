// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/private/resources.h>
#include <app/private/scheduler.h>
#include <app/resources.h>
#include <app/scheduler.h>

#include <tactility/delay.h>

#include "FreeRTOS.h"
#include "task.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {

// Far above any id the ledger hands out during these tests
constexpr AppInstanceId TRACKED_ID = 0x7FFF0001;

std::atomic<int> deleted_tasks { 0 };

void delete_task(void* handle) {
    deleted_tasks++;
    vTaskDelete(static_cast<TaskHandle_t>(handle));
}

bool is_open(int fd) {
    return fcntl(fd, F_GETFD) != -1;
}

struct TaskParameters {
    AppInstanceId app_instance_id;
    bool ends_by_itself;
};

void task_main(void* context) {
    auto* parameters = static_cast<TaskParameters*>(context);
    app_resources_enter_task(parameters->app_instance_id);
    if (parameters->ends_by_itself) {
        app_resources_untrack_task(xTaskGetCurrentTaskHandle());
        vTaskDelete(nullptr);
    }
    while (true) {
        delay_millis(10);
    }
}

bool create_task(void* context, void** out_handle) {
    TaskHandle_t handle = nullptr;
    if (xTaskCreate(task_main, "resources_test", 4096, context, 1, &handle) != pdPASS) {
        return false;
    }
    *out_handle = handle;
    return true;
}

} // namespace

TEST_CASE("app resources: nothing is tracked without a registered instance") {
    app_scheduler_set_current_app_id(TRACKED_ID);
    CHECK_EQ(app_resources_current_app(), 0);
    const int fd = open("/dev/null", O_RDONLY);
    app_resources_track_fd(fd);
    app_scheduler_set_current_app_id(0);

    REQUIRE_EQ(app_resources_register(TRACKED_ID), ERROR_NONE);
    app_resources_release(TRACKED_ID);
    CHECK(is_open(fd));
    close(fd);
}

TEST_CASE("app resources: release closes and frees what is still tracked") {
    REQUIRE_EQ(app_resources_register(TRACKED_ID), ERROR_NONE);
    app_scheduler_set_current_app_id(TRACKED_ID);
    CHECK_EQ(app_resources_current_app(), TRACKED_ID);

    const int leaked_fd = open("/dev/null", O_RDONLY);
    const int closed_fd = open("/dev/null", O_RDONLY);
    REQUIRE_NE(leaked_fd, -1);
    REQUIRE_NE(closed_fd, -1);
    app_resources_track_fd(leaked_fd);
    app_resources_track_fd(closed_fd);
    app_resources_untrack_fd(closed_fd);

    FILE* file = fopen("/dev/null", "r");
    REQUIRE_NE(file, nullptr);
    app_resources_track_file(file);
    const int file_fd = fileno(file);

    DIR* dir = opendir("/");
    REQUIRE_NE(dir, nullptr);
    app_resources_track_dir(dir);
    const int dir_fd = dirfd(dir);

    app_resources_track_alloc(malloc(16));

    app_scheduler_set_current_app_id(0);
    app_resources_release(TRACKED_ID);

    CHECK_FALSE(is_open(leaked_fd));
    CHECK_FALSE(is_open(file_fd));
    CHECK_FALSE(is_open(dir_fd));
    CHECK(is_open(closed_fd));
    close(closed_fd);

    CHECK_FALSE(app_resources_enter_task(TRACKED_ID));
}

TEST_CASE("app resources: a task that ends within the grace period is not deleted") {
    REQUIRE_EQ(app_resources_register(TRACKED_ID), ERROR_NONE);
    app_scheduler_set_current_app_id(TRACKED_ID);
    deleted_tasks = 0;
    TaskParameters parameters { TRACKED_ID, true };
    CHECK(app_resources_create_task(create_task, &parameters, delete_task, nullptr));
    app_scheduler_set_current_app_id(0);

    app_resources_release(TRACKED_ID);
    CHECK_EQ(deleted_tasks.load(), 0);
}

TEST_CASE("app resources: a task still running after the grace period is deleted") {
    REQUIRE_EQ(app_resources_register(TRACKED_ID), ERROR_NONE);
    app_scheduler_set_current_app_id(TRACKED_ID);
    deleted_tasks = 0;
    TaskParameters parameters { TRACKED_ID, false };
    CHECK(app_resources_create_task(create_task, &parameters, delete_task, nullptr));
    app_scheduler_set_current_app_id(0);

    app_resources_release(TRACKED_ID);
    CHECK_EQ(deleted_tasks.load(), 1);
}
