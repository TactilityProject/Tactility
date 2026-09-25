#include "doctest.h"

#include <cstring>

#include <tactility/filesystem/file_system.h>
#include <tactility/paths.h>

// paths_get_data_path() resolves its root via file_system_find_by_name("data") (see
// TactilityKernel/source/paths.cpp), registered in real builds by platform-esp32/platform-posix's
// own module start(). Tests can't depend on that having found a real "data" directory relative to
// wherever the test binary happens to run, so each test here registers its own fake one instead.

namespace {

error_t fake_mount(void*) { return ERROR_NONE; }
error_t fake_unmount(void*) { return ERROR_NONE; }
// Never actually mount()ed below, so file_system_remove()'s "must be unmounted first"
// precondition needs this to stay false, not true.
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

// Registers a fake "data" filesystem whose path is the literal string "data" (matching what this
// file's assertions used to hardcode, back when paths_get_data_path() had no filesystem to ask),
// for the lifetime of one test case.
struct FakeDataFs {
    char path[16] = "data";
    FileSystem* fs = file_system_add("data", &fake_data_api, path);
    ~FakeDataFs() { file_system_remove(fs); }
};

} // namespace

TEST_CASE("paths_get_data_path succeeds when the buffer exactly fits") {
    FakeDataFs fake;
    char buffer[32] = { 0 };
    CHECK_EQ(paths_get_data_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "data/tactility"), 0);
}

TEST_CASE("paths_get_data_path succeeds with a buffer sized to exactly fit the string and terminator") {
    FakeDataFs fake;
    char buffer[15] = { 0 }; // strlen("data/tactility") + 1
    CHECK_EQ(paths_get_data_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "data/tactility"), 0);
}

TEST_CASE("paths_get_data_path reports a buffer overflow when the buffer is one byte too small") {
    FakeDataFs fake;
    char buffer[14] = { 0 }; // strlen("data/tactility"), no room for the terminator
    CHECK_EQ(paths_get_data_path(buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);
}

TEST_CASE("paths_get_data_path reports a buffer overflow for a zero-size buffer") {
    FakeDataFs fake;
    char buffer[1] = { 'x' };
    CHECK_EQ(paths_get_data_path(buffer, 0), ERROR_BUFFER_OVERFLOW);
    CHECK_EQ(buffer[0], 'x'); // untouched
}

TEST_CASE("paths_get_data_path reports ERROR_NOT_FOUND when no 'data' filesystem is registered") {
    CHECK_EQ(file_system_find_by_name("data"), nullptr); // precondition: nothing left over
    char buffer[32] = { 0 };
    CHECK_EQ(paths_get_data_path(buffer, sizeof(buffer)), ERROR_NOT_FOUND);
}
