#include "doctest.h"

#include <service/paths.h>

#include <tactility/filesystem/file_system.h>
#include <tactility/paths.h>

#include <cstring>
#include <string>

namespace {

// paths_get_data_path() resolves its root via file_system_find_by_name("data") (see
// TactilityKernel/source/paths.cpp), registered in real builds by platform-esp32/platform-posix's
// own module start(). This test binary has no reason to run from a directory with a real "data"
// folder, so a fake one is registered here instead, for every test case in this file.

error_t fake_mount(void*) { return ERROR_NONE; }
error_t fake_unmount(void*) { return ERROR_NONE; }
// Never actually mount()ed below (this fake lives for the whole process), so
// file_system_remove()'s "must be unmounted first" precondition never applies to it anyway.
bool fake_is_mounted(void*) { return false; }

error_t fake_get_path(void* data, char* out_path, size_t out_path_size) {
    const char* path = static_cast<const char*>(data);
    if (std::strlen(path) + 1 > out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    std::strcpy(out_path, path);
    return ERROR_NONE;
}

const FileSystemApi fake_data_api = {
    .mount = fake_mount,
    .unmount = fake_unmount,
    .is_mounted = fake_is_mounted,
    .get_path = fake_get_path,
};

char fake_data_path[] = "data";
FileSystem* fake_data_fs = file_system_add("data", &fake_data_api, fake_data_path);

} // namespace

TEST_CASE("paths_get_data_path returns a non-empty path") {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(paths_get_data_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_GT(std::strlen(buffer), 0);
}

TEST_CASE("paths_get_data_path reports overflow for a too-small buffer") {
    char buffer[1];
    CHECK_EQ(paths_get_data_path(buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);
}

TEST_CASE("service_paths_get_user_data_directory includes the service id") {
    char root[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(paths_get_data_path(root, sizeof(root)), ERROR_NONE);

    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    CHECK_EQ(service_paths_get_user_data_directory("my-service", buffer, sizeof(buffer)), ERROR_NONE);

    std::string expected = std::string(root) + "/service/my-service";
    CHECK_EQ(std::string(buffer), expected);
}

TEST_CASE("service_paths_get_user_data_path appends the child path") {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(service_paths_get_user_data_directory("my-service", directory, sizeof(directory)), ERROR_NONE);

    char buffer[256];
    CHECK_EQ(service_paths_get_user_data_path("my-service", "settings.properties", buffer, sizeof(buffer)), ERROR_NONE);

    std::string expected = std::string(directory) + "/settings.properties";
    CHECK_EQ(std::string(buffer), expected);
}

TEST_CASE("service_paths_get_assets_directory is nested under the user data directory") {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(service_paths_get_user_data_directory("my-service", directory, sizeof(directory)), ERROR_NONE);

    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    CHECK_EQ(service_paths_get_assets_directory("my-service", buffer, sizeof(buffer)), ERROR_NONE);

    std::string expected = std::string(directory) + "/assets";
    CHECK_EQ(std::string(buffer), expected);
}

TEST_CASE("service_paths_get_assets_path appends the child path") {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(service_paths_get_assets_directory("my-service", directory, sizeof(directory)), ERROR_NONE);

    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    CHECK_EQ(service_paths_get_assets_path("my-service", "icon.png", buffer, sizeof(buffer)), ERROR_NONE);

    std::string expected = std::string(directory) + "/icon.png";
    CHECK_EQ(std::string(buffer), expected);
}

TEST_CASE("service_paths functions report overflow for a too-small buffer") {
    char buffer[1];
    CHECK_EQ(service_paths_get_user_data_directory("my-service", buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);
    CHECK_EQ(service_paths_get_user_data_path("my-service", "child", buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);
    CHECK_EQ(service_paths_get_assets_directory("my-service", buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);
    CHECK_EQ(service_paths_get_assets_path("my-service", "child", buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);
}
