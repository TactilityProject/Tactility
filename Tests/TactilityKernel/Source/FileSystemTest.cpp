#include "doctest.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <tactility/filesystem/file_system.h>

static int mount_called = 0;
static int unmount_called = 0;
static bool mounted_state = false;
static error_t mount_result = ERROR_NONE;
static error_t unmount_result = ERROR_NONE;

static error_t test_mount(void*) {
    mount_called++;
    mounted_state = true;
    return mount_result;
}

static error_t test_unmount(void*) {
    unmount_called++;
    mounted_state = false;
    return unmount_result;
}

static bool test_is_mounted(void*) {
    return mounted_state;
}

static error_t test_get_path(void* data, char* out_path, size_t out_path_size) {
    const char* path = static_cast<const char*>(data);
    if (std::strlen(path) + 1 > out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    std::strcpy(out_path, path);
    return ERROR_NONE;
}

static const FileSystemApi test_api = {
    .mount = test_mount,
    .unmount = test_unmount,
    .is_mounted = test_is_mounted,
    .get_path = test_get_path
};

static void reset_counters() {
    mount_called = 0;
    unmount_called = 0;
    mounted_state = false;
    mount_result = ERROR_NONE;
    unmount_result = ERROR_NONE;
}

TEST_CASE("file_system_mount/unmount delegate to the api and reflect is_mounted state") {
    reset_counters();
    char path_data[] = "some/path";
    FileSystem* fs = file_system_add(&test_api, path_data);

    CHECK_EQ(file_system_is_mounted(fs), false);
    CHECK_EQ(file_system_mount(fs), ERROR_NONE);
    CHECK_EQ(mount_called, 1);
    CHECK_EQ(file_system_is_mounted(fs), true);

    CHECK_EQ(file_system_unmount(fs), ERROR_NONE);
    CHECK_EQ(unmount_called, 1);
    CHECK_EQ(file_system_is_mounted(fs), false);

    file_system_remove(fs);
}

TEST_CASE("file_system_mount propagates api failure without changing state on its own") {
    reset_counters();
    mount_result = ERROR_RESOURCE;
    char path_data[] = "some/path";
    FileSystem* fs = file_system_add(&test_api, path_data);

    CHECK_EQ(file_system_mount(fs), ERROR_RESOURCE);
    // The fake api still flips mounted_state; file_system itself has no independent state,
    // it always defers to the api's is_mounted().
    CHECK_EQ(file_system_is_mounted(fs), true);

    mounted_state = false;
    file_system_remove(fs);
}

TEST_CASE("file_system_get_path forwards to the api with the caller's buffer size") {
    reset_counters();
    char path_data[] = "mount/point";
    FileSystem* fs = file_system_add(&test_api, path_data);

    char small_buffer[4];
    CHECK_EQ(file_system_get_path(fs, small_buffer, sizeof(small_buffer)), ERROR_BUFFER_OVERFLOW);

    char big_buffer[32];
    CHECK_EQ(file_system_get_path(fs, big_buffer, sizeof(big_buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(big_buffer, "mount/point"), 0);

    file_system_remove(fs);
}

TEST_CASE("file_system_set_owner/get_owner round-trip and default to null") {
    reset_counters();
    char path_data[] = "some/path";
    FileSystem* fs = file_system_add(&test_api, path_data);

    CHECK_EQ(file_system_get_owner(fs), nullptr);

    auto* fake_owner = reinterpret_cast<Device*>(0x1234);
    file_system_set_owner(fs, fake_owner);
    CHECK_EQ(file_system_get_owner(fs), fake_owner);

    file_system_set_owner(fs, nullptr);
    CHECK_EQ(file_system_get_owner(fs), nullptr);

    file_system_remove(fs);
}

TEST_CASE("file_system_for_each visits every registered file system") {
    reset_counters();
    char path_a[] = "a";
    char path_b[] = "b";
    char path_c[] = "c";
    FileSystem* fs_a = file_system_add(&test_api, path_a);
    FileSystem* fs_b = file_system_add(&test_api, path_b);
    FileSystem* fs_c = file_system_add(&test_api, path_c);

    std::vector<FileSystem*> visited;
    file_system_for_each(&visited, [](FileSystem* fs, void* context) {
        static_cast<std::vector<FileSystem*>*>(context)->push_back(fs);
        return true;
    });

    CHECK_EQ(visited.size(), 3);
    CHECK(std::find(visited.begin(), visited.end(), fs_a) != visited.end());
    CHECK(std::find(visited.begin(), visited.end(), fs_b) != visited.end());
    CHECK(std::find(visited.begin(), visited.end(), fs_c) != visited.end());

    file_system_remove(fs_a);
    file_system_remove(fs_b);
    file_system_remove(fs_c);
}

TEST_CASE("file_system_for_each stops early when the callback returns false") {
    reset_counters();
    char path_a[] = "a";
    char path_b[] = "b";
    FileSystem* fs_a = file_system_add(&test_api, path_a);
    FileSystem* fs_b = file_system_add(&test_api, path_b);

    int visit_count = 0;
    file_system_for_each(&visit_count, [](FileSystem*, void* context) {
        (*static_cast<int*>(context))++;
        return false; // stop after the first entry
    });

    CHECK_EQ(visit_count, 1);

    file_system_remove(fs_a);
    file_system_remove(fs_b);
}

TEST_CASE("file_system_remove drops the file system from subsequent iteration") {
    reset_counters();
    char path_a[] = "a";
    char path_b[] = "b";
    FileSystem* fs_a = file_system_add(&test_api, path_a);
    FileSystem* fs_b = file_system_add(&test_api, path_b);

    file_system_remove(fs_a);

    std::vector<FileSystem*> visited;
    file_system_for_each(&visited, [](FileSystem* fs, void* context) {
        static_cast<std::vector<FileSystem*>*>(context)->push_back(fs);
        return true;
    });

    CHECK_EQ(visited.size(), 1);
    CHECK_EQ(visited[0], fs_b);

    file_system_remove(fs_b);
}
