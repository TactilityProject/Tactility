#include "doctest.h"

#include <Tactility/app/shell/Shell.h>

#include <tactility/paths.h>

#include <string>
#include <sys/stat.h>

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
    mkdir(base.c_str(), 0755);
    mkdir((base + "/appfolder").c_str(), 0755);

    // A path typed as the command completes like any other path
    CHECK_EQ(complete(base + "/app"), "folder/");
    CHECK_EQ(complete("ls " + base + "/app"), "folder/");
    // A relative first word completes against the working directory, which is "/" without an app instance
    CHECK_EQ(complete("./tm"), "p/");
    CHECK_EQ(complete("tm"), "p/");
    CHECK_EQ(complete("./nothing"), "<none>");
}
