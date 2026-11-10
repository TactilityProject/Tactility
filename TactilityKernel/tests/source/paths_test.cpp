#include "doctest.h"

#include <cstring>
#include <string>

#include <tactility/paths.h>

namespace {

const std::string ROOT = CONFIG_TT_ROOT_PATH;
const std::string DATA_PATH = ROOT + "/tactility";
const std::string TEMP_PATH = ROOT + "/tactility/tmp";

} // namespace

TEST_CASE("paths_get_root_path returns the configured root path") {
    char buffer[FILE_MAX_PATH_STRING_LENGTH] = { 0 };
    CHECK_EQ(paths_get_root_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(ROOT, std::string(buffer));
}

TEST_CASE("paths_get_root_path succeeds with a buffer sized to exactly fit the string and terminator") {
    std::string buffer(ROOT.size() + 1, 'x');
    CHECK_EQ(paths_get_root_path(buffer.data(), buffer.size()), ERROR_NONE);
    CHECK_EQ(ROOT, std::string(buffer.c_str()));
}

TEST_CASE("paths_get_root_path reports a buffer overflow when the buffer is one byte too small") {
    std::string buffer(ROOT.size(), 'x');
    CHECK_EQ(paths_get_root_path(buffer.data(), buffer.size()), ERROR_BUFFER_OVERFLOW);
}

TEST_CASE("paths_get_data_path appends the tactility folder to the root") {
    char buffer[FILE_MAX_PATH_STRING_LENGTH] = { 0 };
    CHECK_EQ(paths_get_data_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(DATA_PATH, std::string(buffer));
}

TEST_CASE("paths_get_data_path succeeds with a buffer sized to exactly fit the string and terminator") {
    std::string buffer(DATA_PATH.size() + 1, 'x');
    CHECK_EQ(paths_get_data_path(buffer.data(), buffer.size()), ERROR_NONE);
    CHECK_EQ(DATA_PATH, std::string(buffer.c_str()));
}

TEST_CASE("paths_get_data_path reports a buffer overflow when the buffer is one byte too small") {
    std::string buffer(DATA_PATH.size(), 'x');
    CHECK_EQ(paths_get_data_path(buffer.data(), buffer.size()), ERROR_BUFFER_OVERFLOW);
}

TEST_CASE("paths_get_data_path reports a buffer overflow for a zero-size buffer") {
    char buffer[1] = { 'x' };
    CHECK_EQ(paths_get_data_path(buffer, 0), ERROR_BUFFER_OVERFLOW);
    CHECK_EQ(buffer[0], 'x'); // untouched
}

TEST_CASE("paths_get_temp_path appends tactility/tmp to the root") {
    char buffer[FILE_MAX_PATH_STRING_LENGTH] = { 0 };
    CHECK_EQ(paths_get_temp_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(TEMP_PATH, std::string(buffer));
}

TEST_CASE("paths_get_temp_path reports a buffer overflow when the buffer is one byte too small") {
    std::string buffer(TEMP_PATH.size(), 'x');
    CHECK_EQ(paths_get_temp_path(buffer.data(), buffer.size()), ERROR_BUFFER_OVERFLOW);
}
