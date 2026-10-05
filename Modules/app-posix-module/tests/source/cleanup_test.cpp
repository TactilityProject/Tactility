// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/loader.h>
#include <app/manager.h>
#include <app/start.h>

#include <service/manager.h>

#include <tactility/delay.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

extern ServiceManifest loader_service_manifest;

namespace {

struct LeakedFds {
    int fd = -1;
    int file_fd = -1;
    int dir_fd = -1;
};

bool is_open(int fd) {
    return fcntl(fd, F_GETFD) != -1;
}

/** Runs the leak fixture as a top-level app and reads back the fds it leaked. */
bool run_leak_fixture(uint8_t flags, bool with_thread, LeakedFds& out_fds) {
    if (service_manager_find_instance(APP_LOADER_PATH_SERVICE_ID) == nullptr) {
        service_manager_add(&loader_service_manifest, /*auto_start=*/true);
    }

    char output_path[] = "/tmp/cleanup_test_XXXXXX";
    const int output_fd = mkstemp(output_path);
    if (output_fd == -1) {
        return false;
    }
    close(output_fd);

    AppManifest manifest { "test.posix.leak", "Leak", APP_CATEGORY_USER, { APP_LOCATION_PATH, const_cast<char*>(LEAK_FIXTURE_APP_PATH) }, flags };
    REQUIRE_EQ(app_manager_add(&manifest), ERROR_NONE);

    const char* argv[] = { LEAK_FIXTURE_APP_PATH, output_path, with_thread ? "thread" : "" };
    AppStartContext context;
    REQUIRE_EQ(app_start_context_from_id("test.posix.leak", &context), ERROR_NONE);
    app_start_context_set_arguments_ext(&context, 3, argv);
    AppInstanceId app_instance_id = 0;
    REQUIRE_EQ(app_start_with_context(&context, &app_instance_id), ERROR_NONE);

    // Beyond the task grace period and thread join timeout
    for (int waited = 0; waited < 5000 && app_manager_get_state(app_instance_id) != APP_INSTANCE_STATE_STOPPED; waited += 10) {
        delay_millis(10);
    }
    const bool stopped = app_manager_get_state(app_instance_id) == APP_INSTANCE_STATE_STOPPED;
    app_manager_remove("test.posix.leak");

    FILE* output = fopen(output_path, "r");
    const bool read = output != nullptr && fscanf(output, "%d %d %d", &out_fds.fd, &out_fds.file_fd, &out_fds.dir_fd) == 3;
    if (output != nullptr) {
        fclose(output);
    }
    unlink(output_path);
    return stopped && read;
}

} // namespace

TEST_CASE("an app with APP_MANIFEST_FLAG_CLEANUP has its leaked files closed when it ends") {
    LeakedFds fds;
    REQUIRE(run_leak_fixture(APP_MANIFEST_FLAG_CLEANUP, false, fds));
    CHECK_FALSE(is_open(fds.fd));
    CHECK_FALSE(is_open(fds.file_fd));
    CHECK_FALSE(is_open(fds.dir_fd));
}

TEST_CASE("an app with APP_MANIFEST_FLAG_CLEANUP has its leftover thread ended before it's unloaded") {
    LeakedFds fds;
    REQUIRE(run_leak_fixture(APP_MANIFEST_FLAG_CLEANUP, true, fds));
    CHECK_FALSE(is_open(fds.fd));
}

TEST_CASE("an app without APP_MANIFEST_FLAG_CLEANUP keeps leaking") {
    LeakedFds fds;
    REQUIRE(run_leak_fixture(0, false, fds));
    CHECK(is_open(fds.fd));
    CHECK(is_open(fds.file_fd));
    CHECK(is_open(fds.dir_fd));
    close(fds.fd);
    close(fds.file_fd);
    close(fds.dir_fd);
}
