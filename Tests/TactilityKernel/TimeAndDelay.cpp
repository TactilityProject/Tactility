#include "doctest.h"
#include <Tactility/delay.h>
#include <Tactility/time.h>

TEST_CASE("delay ticks should be accurate within 1 tick") {
    auto start_time = get_ticks();
    delay_ticks(100);
    auto end_time = get_ticks();
    auto difference = end_time - start_time;
    CHECK_EQ(difference >= 100, true);
    CHECK_EQ(difference <= 101, true);
}

TEST_CASE("delay millis should be accurate within 1 tick") {
    auto start_time = get_millis();
    delay_millis(100);
    auto end_time = get_millis();
    auto difference = end_time - start_time;
    CHECK_EQ(difference >= 100, true);
    CHECK_EQ(difference <= 101, true);
}

TEST_CASE("microsecond time should be accurate within 1 tick") {
    auto start_time = get_micros_since_boot();
    delay_millis(100);
    auto end_time = get_micros_since_boot();
    auto difference = (end_time - start_time) / 1000;
    CHECK_EQ(difference >= 99, true);
    CHECK_EQ(difference <= 101, true);
}
