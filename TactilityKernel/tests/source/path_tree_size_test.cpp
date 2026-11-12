#include "doctest.h"

#include <tactility/filesystem/fs.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void write_file(const std::string& path, size_t size) {
    FILE* file = fopen(path.c_str(), "wb");
    REQUIRE(file != nullptr);
    const std::string content(size, 'x');
    fwrite(content.data(), 1, content.size(), file);
    fclose(file);
}

}

TEST_CASE("path_tree_size sums files in a deep tree and in a wide directory") {
    char pattern[] = "/tmp/tt_path_tree_size_XXXXXX";
    REQUIRE(mkdtemp(pattern) != nullptr);
    const std::string root = pattern;

    // Deeper than a recursive walk with a fixed buffer per level could handle, and more subdirectories than a single pass held
    std::string deep = root;
    for (int i = 0; i < 40; i++) {
        deep += "/d";
        REQUIRE(directory_make(deep.c_str(), false) == ERROR_NONE);
        write_file(deep + "/f", 10);
    }
    for (int i = 0; i < 50; i++) {
        const std::string wide = root + "/w" + std::to_string(i);
        REQUIRE(directory_make(wide.c_str(), false) == ERROR_NONE);
        write_file(wide + "/f", 100);
    }
    write_file(root + "/top", 1);

    CHECK_EQ(path_tree_size(root.c_str()), 40 * 10 + 50 * 100 + 1);
    CHECK_EQ(path_tree_size((root + "/top").c_str()), 1);
    CHECK_EQ(path_tree_size((root + "/missing").c_str()), 0);

    std::filesystem::remove_all(root);
}

TEST_CASE("file_exists is true for a regular file only") {
    char pattern[] = "/tmp/tt_file_exists_XXXXXX";
    REQUIRE(mkdtemp(pattern) != nullptr);
    const std::string root = pattern;
    write_file(root + "/file", 1);

    CHECK(file_exists((root + "/file").c_str()));
    CHECK_FALSE(file_exists(root.c_str()));
    CHECK_FALSE(file_exists((root + "/missing").c_str()));

    // Exists, but isn't a regular file
    REQUIRE_EQ(mkfifo((root + "/fifo").c_str(), 0600), 0);
    CHECK(path_exists((root + "/fifo").c_str()));
    CHECK_FALSE(file_exists((root + "/fifo").c_str()));

    std::filesystem::remove_all(root);
}

TEST_CASE("file_copy reports a source that can't be read instead of copying nothing") {
    char pattern[] = "/tmp/tt_file_copy_error_XXXXXX";
    REQUIRE(mkdtemp(pattern) != nullptr);
    const std::string root = pattern;
    REQUIRE(directory_make((root + "/source").c_str(), false) == ERROR_NONE);

    CHECK_EQ(file_copy((root + "/source").c_str(), (root + "/target").c_str(), false), ERROR_RESOURCE);

    std::filesystem::remove_all(root);
}

TEST_CASE("file_copy without overwrite never touches an existing target") {
    char pattern[] = "/tmp/tt_file_copy_exclusive_XXXXXX";
    REQUIRE(mkdtemp(pattern) != nullptr);
    const std::string root = pattern;
    write_file(root + "/source", 10);
    write_file(root + "/target", 3);

    CHECK_EQ(file_copy((root + "/source").c_str(), (root + "/target").c_str(), false), ERROR_ALREADY_EXISTS);
    CHECK_EQ(path_tree_size((root + "/target").c_str()), 3);

    CHECK_EQ(file_copy((root + "/source").c_str(), (root + "/target").c_str(), true), ERROR_NONE);
    CHECK_EQ(path_tree_size((root + "/target").c_str()), 10);

    // Neither a missing source nor an empty one leaves a wrong target behind
    CHECK_EQ(file_copy((root + "/missing").c_str(), (root + "/copy").c_str(), false), ERROR_NOT_FOUND);
    CHECK_FALSE(path_exists((root + "/copy").c_str()));
    write_file(root + "/empty", 0);
    CHECK_EQ(file_copy((root + "/empty").c_str(), (root + "/empty_copy").c_str(), false), ERROR_NONE);
    CHECK(file_exists((root + "/empty_copy").c_str()));

    // An empty source empties an existing target
    write_file(root + "/full", 7);
    CHECK_EQ(file_copy((root + "/empty").c_str(), (root + "/full").c_str(), true), ERROR_NONE);
    CHECK_EQ(path_tree_size((root + "/full").c_str()), 0);

    std::filesystem::remove_all(root);
}

TEST_CASE("file_copy rejects another name of the same file") {
    char pattern[] = "/tmp/tt_file_copy_alias_XXXXXX";
    REQUIRE(mkdtemp(pattern) != nullptr);
    const std::string root = pattern;
    write_file(root + "/source", 10);
    REQUIRE(link((root + "/source").c_str(), (root + "/alias").c_str()) == 0);

    CHECK_EQ(file_copy((root + "/source").c_str(), (root + "/alias").c_str(), true), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(file_copy((root + "/source").c_str(), (root + "//source").c_str(), true), ERROR_INVALID_ARGUMENT);
    CHECK_EQ(path_tree_size((root + "/source").c_str()), 10);

    std::filesystem::remove_all(root);
}

TEST_CASE("path_tree_size doesn't repeat directories that links lead back to") {
    char pattern[] = "/tmp/tt_path_tree_size_loop_XXXXXX";
    REQUIRE(mkdtemp(pattern) != nullptr);
    const std::string root = pattern;
    write_file(root + "/f", 10);
    // Two links to the directory itself would make the walk visit it exponentially often
    REQUIRE(symlink(root.c_str(), (root + "/a").c_str()) == 0);
    REQUIRE(symlink(root.c_str(), (root + "/b").c_str()) == 0);

    CHECK_EQ(path_tree_size(root.c_str()), 10);

    std::filesystem::remove_all(root);
}
