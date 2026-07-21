#include "doctest.h"

#include <cstring>

#include <tactility/paths.h>

// The simulator target is never built with ESP_PLATFORM, so paths_get_user_data_path()
// always takes the fixed "data" path branch here, guarded by a buffer-size check.

TEST_CASE("paths_get_user_data_path succeeds when the buffer exactly fits") {
    char buffer[16] = { 0 };
    CHECK_EQ(paths_get_user_data_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "data"), 0);
}

TEST_CASE("paths_get_user_data_path succeeds with a buffer sized to exactly fit the string and terminator") {
    char buffer[5] = { 0 }; // strlen("data") + 1
    CHECK_EQ(paths_get_user_data_path(buffer, sizeof(buffer)), ERROR_NONE);
    CHECK_EQ(std::strcmp(buffer, "data"), 0);
}

TEST_CASE("paths_get_user_data_path reports a buffer overflow when the buffer is one byte too small") {
    char buffer[4] = { 0 }; // strlen("data"), no room for the terminator
    CHECK_EQ(paths_get_user_data_path(buffer, sizeof(buffer)), ERROR_BUFFER_OVERFLOW);
}

TEST_CASE("paths_get_user_data_path reports a buffer overflow for a zero-size buffer") {
    char buffer[1] = { 'x' };
    CHECK_EQ(paths_get_user_data_path(buffer, 0), ERROR_BUFFER_OVERFLOW);
    CHECK_EQ(buffer[0], 'x'); // untouched
}
