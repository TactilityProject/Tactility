#include "doctest.h"

#include <tactility/filesystem/fs.h>

#include <cstdio>
#include <filesystem>
#include <string>
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
