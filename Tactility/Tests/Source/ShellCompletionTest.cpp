#include "doctest.h"

#include <Tactility/app/shell/Shell.h>

#include <app/manager.h>

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

TEST_CASE("shell completion: installed headless apps complete by their binary name") {
    AppManifest headless {};
    snprintf(headless.id, sizeof(headless.id), "test.shell.installed");
    snprintf(headless.name, sizeof(headless.name), "Installed");
    headless.flags = APP_MANIFEST_FLAG_HEADLESS;
    headless.location = { APP_LOCATION_PATH, const_cast<char*>("/nonexistent/bin/posix-x86_64/installedcmd.so") };
    REQUIRE_EQ(app_manager_add(&headless), ERROR_NONE);

    AppManifest windowed {};
    snprintf(windowed.id, sizeof(windowed.id), "test.shell.windowed");
    snprintf(windowed.name, sizeof(windowed.name), "Windowed");
    windowed.location = { APP_LOCATION_PATH, const_cast<char*>("/nonexistent/bin/posix-x86_64/windowedcmd.so") };
    REQUIRE_EQ(app_manager_add(&windowed), ERROR_NONE);

    CHECK_EQ(complete("installedc"), "md ");
    CHECK_EQ(complete("windowedc"), "<none>");

    app_manager_remove("test.shell.windowed");
    app_manager_remove("test.shell.installed");
}
