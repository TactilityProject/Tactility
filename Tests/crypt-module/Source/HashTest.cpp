#include "doctest.h"
#include <tactility/hash.h>
#include <cstring>

TEST_CASE("djb2_str of an empty string returns the DJB2 seed value") {
    CHECK_EQ(djb2_str(""), 5381u);
}

TEST_CASE("djb2_str produces the well-known DJB2 hash for a string") {
    CHECK_EQ(djb2_str("hello"), 261238937u);
}

TEST_CASE("djb2_str is deterministic for the same input") {
    CHECK_EQ(djb2_str("tactility"), djb2_str("tactility"));
}

TEST_CASE("djb2_str produces different hashes for different input") {
    CHECK_NE(djb2_str("tactility"), djb2_str("Tactility"));
}

TEST_CASE("djb2_data of an empty buffer returns the DJB2 seed value") {
    CHECK_EQ(djb2_data("", 0), 5381u);
}

TEST_CASE("djb2_data matches djb2_str for the same bytes") {
    const char* text = "tactility";
    CHECK_EQ(djb2_data(text, strlen(text)), djb2_str(text));
}
