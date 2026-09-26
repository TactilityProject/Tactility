#include "doctest.h"

#include <Tactility/app/shell/Shell.h>

#include <tactility/filesystem/fs.h>
#include <tactility/paths.h>

#include <cstdio>
#include <string>

namespace {

std::string complete(const std::string& line) {
    char suffix[FILE_MAX_PATH_STRING_LENGTH];
    bool listed = false;
    if (!Shell::complete(line.c_str(), suffix, sizeof(suffix), &listed)) {
        return "<none>";
    }
    return suffix;
}

} // namespace

TEST_CASE("shell completion: first word completes directories as well as commands") {
    char temp[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(paths_get_temp_path(temp, sizeof(temp)), ERROR_NONE);
    const std::string base = std::string(temp) + "/completiontest";
    REQUIRE_EQ(directory_make((base + "/appfolder").c_str(), true), ERROR_NONE);

    // A path typed as the command completes like any other path
    CHECK_EQ(complete(base + "/app"), "folder/");
    CHECK_EQ(complete("ls " + base + "/app"), "folder/");
    // A relative first word completes against the working directory, which is "/" without an app instance
    CHECK_EQ(complete("./tm"), "p/");
    CHECK_EQ(complete("tm"), "p/");
    CHECK_EQ(complete("./nothing"), "<none>");
}

TEST_CASE("shell completion: plain files complete when the word names a path") {
    char temp[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(paths_get_temp_path(temp, sizeof(temp)), ERROR_NONE);
    const std::string base = std::string(temp) + "/completiontest";
    REQUIRE_EQ(directory_make(base.c_str(), true), ERROR_NONE);
    FILE* file = fopen((base + "/runme.sh").c_str(), "w");
    REQUIRE_NE(file, nullptr);
    fclose(file);

    // With a '/', the file can be run, so it completes
    CHECK_EQ(complete(base + "/run"), "me.sh ");
    // As an argument it completes too
    CHECK_EQ(complete("cat " + base + "/run"), "me.sh ");
}
