#include "doctest.h"
#include <tactility/properties_file.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

const char* TEST_PATH = "/tmp/tactility_kernel_properties_file_test.properties";

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

void write_raw(const char* path, const char* content) {
    FILE* file = std::fopen(path, "w");
    std::fputs(content, file);
    std::fclose(file);
}

} // namespace

TEST_CASE("properties_file_open on a missing file starts out empty, without creating it") {
    ScratchFile scratch;

    PropertiesFile* file = properties_file_open(TEST_PATH);
    CHECK_NE(file, nullptr);
    CHECK_FALSE(properties_file_has(file, "key"));
    CHECK_FALSE(file_exists(TEST_PATH));

    properties_file_close(file);
}

TEST_CASE("properties_file_open returns NULL when a genuine I/O error interrupts reading") {
    // fopen() on a directory succeeds on Linux, but the first read fails with EISDIR and sets
    // the stream's error indicator - a deterministic way to exercise load_from_file()'s
    // ferror() check without needing real storage-hardware fault injection.
    const char* dir_path = "/tmp/tactility_kernel_properties_file_test_is_a_directory";
    rmdir(dir_path);
    REQUIRE_EQ(mkdir(dir_path, 0777), 0);

    CHECK_EQ(properties_file_open(dir_path), nullptr);

    rmdir(dir_path);
}

TEST_CASE("set/has/get round-trip, and close persists while unclosed changes don't") {
    ScratchFile scratch;

    PropertiesFile* file = properties_file_open(TEST_PATH);
    properties_file_set(file, "key", "value");

    CHECK(properties_file_has(file, "key"));
    char buffer[32];
    CHECK_EQ(properties_file_get(file, "key", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "value"), 0);

    // Not persisted yet - only properties_file_close() writes to disk.
    CHECK_FALSE(file_exists(TEST_PATH));

    properties_file_close(file);
    CHECK(file_exists(TEST_PATH));

    PropertiesFile* reopened = properties_file_open(TEST_PATH);
    CHECK(properties_file_has(reopened, "key"));
    properties_file_close(reopened);
}

TEST_CASE("properties_file_get reports ERROR_BUFFER_OVERFLOW and ERROR_NOT_FOUND") {
    ScratchFile scratch;

    PropertiesFile* file = properties_file_open(TEST_PATH);
    properties_file_set(file, "key", "value");

    char tiny[3];
    CHECK_EQ(properties_file_get(file, "key", tiny, sizeof(tiny)), ERROR_BUFFER_OVERFLOW);

    char buffer[32];
    CHECK_EQ(properties_file_get(file, "missing", buffer, sizeof(buffer)), ERROR_NOT_FOUND);

    properties_file_close(file);
}

TEST_CASE("set overwrites a previously stored value") {
    ScratchFile scratch;

    PropertiesFile* file = properties_file_open(TEST_PATH);
    properties_file_set(file, "key", "first");
    properties_file_set(file, "key", "second");

    char buffer[32];
    CHECK_EQ(properties_file_get(file, "key", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "second"), 0);

    properties_file_close(file);
}

TEST_CASE("comments and blank lines are skipped, keys and values are trimmed") {
    ScratchFile scratch;
    write_raw(TEST_PATH,
        "# Comment\n"
        " \t# Indented comment\n"
        "\n"
        "key1=value1\n"
        " \tkey 2\t = \tvalue 2\t \n");

    PropertiesFile* file = properties_file_open(TEST_PATH);

    char buffer[32];
    CHECK_EQ(properties_file_get(file, "key1", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "value1"), 0);

    // Only leading/trailing whitespace is trimmed - the internal space in "key 2"/"value 2"
    // survives.
    CHECK_EQ(properties_file_get(file, "key 2", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "value 2"), 0);

    properties_file_close(file);
}

TEST_CASE("a malformed line (no '=') is skipped without aborting the rest of the file") {
    ScratchFile scratch;
    write_raw(TEST_PATH, "not_a_key_value_pair\nkey=value\n");

    PropertiesFile* file = properties_file_open(TEST_PATH);
    CHECK(properties_file_has(file, "key"));
    CHECK_FALSE(properties_file_has(file, "not_a_key_value_pair"));
    properties_file_close(file);
}

TEST_CASE("a [section] line prefixes every following key until the next section") {
    ScratchFile scratch;
    write_raw(TEST_PATH,
        "[app]\n"
        "id=one.tactility.helloworld\n"
        "name=Hello\n"
        "[other]\n"
        "id=x\n");

    PropertiesFile* file = properties_file_open(TEST_PATH);

    char buffer[64];
    CHECK_EQ(properties_file_get(file, "[app]id", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "one.tactility.helloworld"), 0);

    CHECK_EQ(properties_file_get(file, "[app]name", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "Hello"), 0);

    CHECK_EQ(properties_file_get(file, "[other]id", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "x"), 0);

    CHECK_FALSE(properties_file_has(file, "id"));

    properties_file_close(file);
}

TEST_CASE("properties_file_close reports ERROR_NONE on success") {
    ScratchFile scratch;

    PropertiesFile* file = properties_file_open(TEST_PATH);
    properties_file_set(file, "key", "value");

    CHECK_EQ(properties_file_close(file), ERROR_NONE);
}

TEST_CASE("properties_file_close reports ERROR_RESOURCE when the parent directory doesn't exist") {
    const char* path = "/tmp/tactility_kernel_properties_file_test_missing_dir/settings.properties";
    std::remove(path); // no-op if the directory doesn't exist, which is the point of this test

    // Missing directory is not an error for open() - it starts out empty, same as a missing
    // file (see the "starts out empty" test above).
    PropertiesFile* file = properties_file_open(path);
    REQUIRE_NE(file, nullptr);
    properties_file_set(file, "key", "value");

    // close()'s save can't create its temp file in a directory that doesn't exist.
    CHECK_EQ(properties_file_close(file), ERROR_RESOURCE);
}

TEST_CASE("a failed close leaves previously-saved content on disk untouched") {
    if (geteuid() == 0) {
        // Root bypasses directory write permissions, so the read-only directory below would
        // not make save_to_file() fail.
        return;
    }

    const char* dir = "/tmp/tactility_kernel_properties_file_readonly_test";
    const char* path = "/tmp/tactility_kernel_properties_file_readonly_test/settings.properties";

    mkdir(dir, 0700);
    chmod(dir, 0700);
    std::remove(path);

    {
        PropertiesFile* file = properties_file_open(path);
        properties_file_set(file, "key", "original");
        REQUIRE_EQ(properties_file_close(file), ERROR_NONE);
    }

    // Read-only directory - save_to_file()'s temp file can't be created there, so the close
    // below must fail without disturbing the "original" content already on disk.
    REQUIRE_EQ(chmod(dir, 0555), 0);

    PropertiesFile* file = properties_file_open(path);
    properties_file_set(file, "key", "corrupted");
    CHECK_EQ(properties_file_close(file), ERROR_RESOURCE);

    chmod(dir, 0700); // restore write access for the check below and for cleanup

    PropertiesFile* reloaded = properties_file_open(path);
    char buffer[32];
    CHECK_EQ(properties_file_get(reloaded, "key", buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "original"), 0);
    properties_file_close(reloaded);

    std::remove(path);
    rmdir(dir);
}

TEST_CASE("properties_file_for_each visits every key exactly once") {
    ScratchFile scratch;

    PropertiesFile* file = properties_file_open(TEST_PATH);
    properties_file_set(file, "a", "1");
    properties_file_set(file, "b", "2");
    properties_file_set(file, "c", "3");

    std::vector<std::pair<std::string, std::string>> seen;
    properties_file_for_each(file, [](const char* key, const char* value, void* context) {
        auto* out = static_cast<std::vector<std::pair<std::string, std::string>>*>(context);
        out->emplace_back(key, value);
    }, &seen);

    CHECK_EQ(seen.size(), 3);
    for (const auto& [key, value] : seen) {
        if (key == "a") CHECK_EQ(value, "1");
        else if (key == "b") CHECK_EQ(value, "2");
        else if (key == "c") CHECK_EQ(value, "3");
        else FAIL("unexpected key: " << key);
    }

    properties_file_close(file);
}
