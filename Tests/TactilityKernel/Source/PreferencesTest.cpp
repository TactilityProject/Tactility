#include "doctest.h"
#include <tactility/preferences.h>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* TEST_PATH = "/tmp/tactility_kernel_preferences_test.properties";

struct ScratchFile {
    ScratchFile() { std::remove(TEST_PATH); }
    ~ScratchFile() { std::remove(TEST_PATH); }
};

bool file_exists(const char* path) {
    FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
}

bool is_directory(const char* path) {
    struct stat info {};
    return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR;
}

// Writes a raw properties file directly (bypassing preferences_put_*()) so a test can exercise
// a hand-crafted/corrupted payload that preferences_put_*() itself would never produce.
void write_raw(const char* path, const char* content) {
    FILE* file = std::fopen(path, "w");
    std::fputs(content, file);
    std::fclose(file);
}

} // namespace

TEST_CASE("preferences_open_path on a missing file starts out empty, without creating it") {
    ScratchFile scratch;

    Preferences* preferences = preferences_open(TEST_PATH);
    CHECK_NE(preferences, nullptr);
    CHECK_FALSE(preferences_has_bool(preferences, "key"));
    CHECK_FALSE(file_exists(TEST_PATH));

    preferences_close(preferences);
}

TEST_CASE("put_*/has_*/opt_* round-trip all four types") {
    ScratchFile scratch;

    Preferences* preferences = preferences_open(TEST_PATH);
    preferences_put_bool(preferences, "flag", true);
    preferences_put_int32(preferences, "count", -42);
    preferences_put_int64(preferences, "big", 123456789012345LL);
    preferences_put_string(preferences, "text", "hello world");

    CHECK(preferences_has_bool(preferences, "flag"));
    bool bool_out = false;
    CHECK(preferences_opt_bool(preferences, "flag", &bool_out));
    CHECK_EQ(bool_out, true);

    CHECK(preferences_has_int32(preferences, "count"));
    int32_t int32_out = 0;
    CHECK(preferences_opt_int32(preferences, "count", &int32_out));
    CHECK_EQ(int32_out, -42);

    CHECK(preferences_has_int64(preferences, "big"));
    int64_t int64_out = 0;
    CHECK(preferences_opt_int64(preferences, "big", &int64_out));
    CHECK_EQ(int64_out, 123456789012345LL);

    CHECK(preferences_has_string(preferences, "text"));
    char buffer[32];
    CHECK_EQ(preferences_opt_string(preferences, "text", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "hello world"), 0);

    preferences_close(preferences);
}

TEST_CASE("opt_string reports ERROR_BUFFER_OVERFLOW and ERROR_NOT_FOUND") {
    ScratchFile scratch;

    Preferences* preferences = preferences_open(TEST_PATH);
    preferences_put_string(preferences, "text", "hello world");

    char tiny[4];
    CHECK_EQ(preferences_opt_string(preferences, "text", tiny, sizeof(tiny)), ERROR_BUFFER_OVERFLOW);

    char buffer[32];
    CHECK_EQ(preferences_opt_string(preferences, "missing", buffer, sizeof(buffer)), ERROR_NOT_FOUND);

    preferences_close(preferences);
}

TEST_CASE("has_*/opt_* reject a key stored with a different type") {
    ScratchFile scratch;

    Preferences* preferences = preferences_open(TEST_PATH);
    preferences_put_bool(preferences, "key", true);

    CHECK_FALSE(preferences_has_int32(preferences, "key"));
    CHECK_FALSE(preferences_has_int64(preferences, "key"));
    CHECK_FALSE(preferences_has_string(preferences, "key"));

    int32_t out = 0;
    CHECK_FALSE(preferences_opt_int32(preferences, "key", &out));

    preferences_close(preferences);
}

TEST_CASE("has_*/opt_* reject malformed scalar payloads instead of misparsing them") {
    ScratchFile scratch;
    write_raw(TEST_PATH,
        "bad_bool=b:garbage\n"
        "bad_bool_2=b:2\n"
        "trailing_junk=i32:42abc\n"
        "int32_overflow=i32:5000000000\n"
        "int64_overflow=i64:99999999999999999999\n"
        "empty_int=i32:\n");

    Preferences* preferences = preferences_open(TEST_PATH);

    // "b:garbage" must not silently read back as false - has_bool()/opt_bool() must agree it's
    // not a valid bool at all.
    CHECK_FALSE(preferences_has_bool(preferences, "bad_bool"));
    bool bool_out = true;
    CHECK_FALSE(preferences_opt_bool(preferences, "bad_bool", &bool_out));
    CHECK_FALSE(preferences_has_bool(preferences, "bad_bool_2"));
    CHECK_FALSE(preferences_opt_bool(preferences, "bad_bool_2", &bool_out));

    // "42abc" must not silently parse as 42 - the full payload must be consumed.
    CHECK_FALSE(preferences_has_int32(preferences, "trailing_junk"));
    int32_t int32_out = 0;
    CHECK_FALSE(preferences_opt_int32(preferences, "trailing_junk", &int32_out));

    // Fits in a (64-bit, on this platform) `long` but overflows int32_t - must not silently
    // truncate on the narrowing cast.
    CHECK_FALSE(preferences_has_int32(preferences, "int32_overflow"));
    CHECK_FALSE(preferences_opt_int32(preferences, "int32_overflow", &int32_out));

    // Overflows even a 64-bit integer - strtoll() itself reports ERANGE.
    CHECK_FALSE(preferences_has_int64(preferences, "int64_overflow"));
    int64_t int64_out = 0;
    CHECK_FALSE(preferences_opt_int64(preferences, "int64_overflow", &int64_out));

    CHECK_FALSE(preferences_has_int32(preferences, "empty_int"));
    CHECK_FALSE(preferences_opt_int32(preferences, "empty_int", &int32_out));

    preferences_close(preferences);
}

TEST_CASE("a string value with embedded newlines and backslashes survives a reopen") {
    ScratchFile scratch;

    {
        Preferences* preferences = preferences_open(TEST_PATH);
        preferences_put_string(preferences, "text", "line1\nline2 with \\ backslash");
        preferences_close(preferences);
    }
    {
        Preferences* preferences = preferences_open(TEST_PATH);
        char buffer[64];
        CHECK_EQ(preferences_opt_string(preferences, "text", buffer, sizeof(buffer)), ERROR_NONE);
        CHECK_EQ(std::strcmp(buffer, "line1\nline2 with \\ backslash"), 0);
        preferences_close(preferences);
    }
}

TEST_CASE("preferences_close persists changes, and only close persists them") {
    ScratchFile scratch;

    Preferences* preferences = preferences_open(TEST_PATH);
    preferences_put_bool(preferences, "flag", true);

    // Not persisted yet - only preferences_close() writes to disk.
    CHECK_FALSE(file_exists(TEST_PATH));

    preferences_close(preferences);
    CHECK(file_exists(TEST_PATH));

    Preferences* reopened = preferences_open(TEST_PATH);
    CHECK(preferences_has_bool(reopened, "flag"));
    preferences_close(reopened);
}

TEST_CASE("put_* on an already-closed value is visible without reopening") {
    ScratchFile scratch;

    Preferences* preferences = preferences_open(TEST_PATH);
    preferences_put_int32(preferences, "count", 1);
    preferences_close(preferences);

    Preferences* reopened = preferences_open(TEST_PATH);
    preferences_put_int32(reopened, "count", 2);
    int32_t out = 0;
    CHECK(preferences_opt_int32(reopened, "count", &out));
    CHECK_EQ(out, 2);
    preferences_close(reopened);

    Preferences* final_instance = preferences_open(TEST_PATH);
    CHECK(preferences_opt_int32(final_instance, "count", &out));
    CHECK_EQ(out, 2);
    preferences_close(final_instance);
}

TEST_CASE("preferences_open creates missing parent directories (recursively) and persists into them") {
    const char* nested_dir_a = "/tmp/tactility_kernel_preferences_test_nested";
    const char* nested_dir_b = "/tmp/tactility_kernel_preferences_test_nested/a";
    const char* nested_dir_c = "/tmp/tactility_kernel_preferences_test_nested/a/b";
    const char* nested_path = "/tmp/tactility_kernel_preferences_test_nested/a/b/settings.properties";

    std::remove(nested_path);
    rmdir(nested_dir_c);
    rmdir(nested_dir_b);
    rmdir(nested_dir_a);
    REQUIRE_FALSE(is_directory(nested_dir_a));

    Preferences* preferences = preferences_open(nested_path);
    REQUIRE_NE(preferences, nullptr);
    CHECK(is_directory(nested_dir_a));
    CHECK(is_directory(nested_dir_b));
    CHECK(is_directory(nested_dir_c));

    preferences_put_int32(preferences, "count", 7);
    preferences_close(preferences);
    CHECK(file_exists(nested_path));

    Preferences* reopened = preferences_open(nested_path);
    REQUIRE_NE(reopened, nullptr);
    int32_t out = 0;
    CHECK(preferences_opt_int32(reopened, "count", &out));
    CHECK_EQ(out, 7);
    preferences_close(reopened);

    std::remove(nested_path);
    rmdir(nested_dir_c);
    rmdir(nested_dir_b);
    rmdir(nested_dir_a);
}
