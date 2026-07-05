#include "doctest.h"
#include <tactility/paths.h>
#include <tactility/service/service_paths.h>

#include <cstring>
#include <string>

TEST_CASE("paths_get_user_data_path returns a non-empty path") {
    char buffer[192];
    CHECK_EQ(paths_get_user_data_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_GT(std::strlen(buffer), 0);
}

TEST_CASE("paths_get_user_data_path reports overflow for a too-small buffer") {
    char buffer[1];
    CHECK_EQ(paths_get_user_data_path(buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);
}

TEST_CASE("service_paths_get_user_data_directory includes the service id") {
    char root[192];
    REQUIRE_EQ(paths_get_user_data_path(root, sizeof(root)), ERROR_NONE);

    char buffer[224];
    CHECK_EQ(service_paths_get_user_data_directory("my-service", buffer, sizeof(buffer)), ERROR_NONE);

    std::string expected = std::string(root) + "/service/my-service";
    CHECK_EQ(std::string(buffer), expected);
}

TEST_CASE("service_paths_get_user_data_path appends the child path") {
    char directory[224];
    REQUIRE_EQ(service_paths_get_user_data_directory("my-service", directory, sizeof(directory)), ERROR_NONE);

    char buffer[256];
    CHECK_EQ(service_paths_get_user_data_path("my-service", "settings.properties", buffer, sizeof(buffer)), ERROR_NONE);

    std::string expected = std::string(directory) + "/settings.properties";
    CHECK_EQ(std::string(buffer), expected);
}

TEST_CASE("service_paths_get_assets_directory is nested under the user data directory") {
    char directory[224];
    REQUIRE_EQ(service_paths_get_user_data_directory("my-service", directory, sizeof(directory)), ERROR_NONE);

    char buffer[256];
    CHECK_EQ(service_paths_get_assets_directory("my-service", buffer, sizeof(buffer)), ERROR_NONE);

    std::string expected = std::string(directory) + "/assets";
    CHECK_EQ(std::string(buffer), expected);
}

TEST_CASE("service_paths_get_assets_path appends the child path") {
    char directory[224];
    REQUIRE_EQ(service_paths_get_assets_directory("my-service", directory, sizeof(directory)), ERROR_NONE);

    char buffer[256];
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
