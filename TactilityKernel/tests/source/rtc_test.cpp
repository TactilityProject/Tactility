#include "doctest.h"
#include <tactility/drivers/rtc.h>

static RtcDateTime make(uint16_t year, uint8_t month, uint8_t day, uint8_t hour = 0, uint8_t minute = 0, uint8_t second = 0) {
    return RtcDateTime { year, month, day, hour, minute, second };
}

TEST_CASE("rtc_datetime_is_valid accepts boundaries") {
    auto first = make(2000, 1, 1);
    auto last = make(2099, 12, 31, 23, 59, 59);
    CHECK(rtc_datetime_is_valid(&first, 2000, 2099));
    CHECK(rtc_datetime_is_valid(&last, 2000, 2099));
}

TEST_CASE("rtc_datetime_is_valid rejects years outside the range") {
    auto before = make(1999, 6, 15);
    auto after = make(2100, 6, 15);
    CHECK_FALSE(rtc_datetime_is_valid(&before, 2000, 2099));
    CHECK_FALSE(rtc_datetime_is_valid(&after, 2000, 2099));
}

TEST_CASE("rtc_datetime_is_valid rejects out of range fields") {
    auto month0 = make(2024, 0, 1);
    auto month13 = make(2024, 13, 1);
    auto day0 = make(2024, 1, 0);
    auto hour24 = make(2024, 1, 1, 24);
    auto minute60 = make(2024, 1, 1, 0, 60);
    auto second60 = make(2024, 1, 1, 0, 0, 60);
    CHECK_FALSE(rtc_datetime_is_valid(&month0, 2000, 2199));
    CHECK_FALSE(rtc_datetime_is_valid(&month13, 2000, 2199));
    CHECK_FALSE(rtc_datetime_is_valid(&day0, 2000, 2199));
    CHECK_FALSE(rtc_datetime_is_valid(&hour24, 2000, 2199));
    CHECK_FALSE(rtc_datetime_is_valid(&minute60, 2000, 2199));
    CHECK_FALSE(rtc_datetime_is_valid(&second60, 2000, 2199));
}

TEST_CASE("rtc_datetime_is_valid checks the day against the month length") {
    auto apr31 = make(2024, 4, 31);
    auto apr30 = make(2024, 4, 30);
    auto jan31 = make(2024, 1, 31);
    CHECK_FALSE(rtc_datetime_is_valid(&apr31, 2000, 2199));
    CHECK(rtc_datetime_is_valid(&apr30, 2000, 2199));
    CHECK(rtc_datetime_is_valid(&jan31, 2000, 2199));
}

TEST_CASE("rtc_datetime_is_valid handles leap years") {
    auto leap = make(2024, 2, 29);
    auto common = make(2023, 2, 29);
    auto century = make(2100, 2, 29);
    auto fourHundred = make(2000, 2, 29);
    CHECK(rtc_datetime_is_valid(&leap, 2000, 2199));
    CHECK_FALSE(rtc_datetime_is_valid(&common, 2000, 2199));
    CHECK_FALSE(rtc_datetime_is_valid(&century, 2000, 2199));
    CHECK(rtc_datetime_is_valid(&fourHundred, 2000, 2199));
}
