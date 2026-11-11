#include "doctest.h"

#include <tactility/drivers/radio.h>
#include <tactility/paths.h>
#include <tactility/radio_settings.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

const char* TEST_ROOT = "/tmp/tactility_kernel_radio_settings_test";
const char* SETTINGS_PATH = "/tmp/tactility_kernel_radio_settings_test/tactility/settings/radio.properties";

// Uses a scratch directory as the root path for the lifetime of one test case
struct FakeDataFs {
    FakeDataFs() {
        std::filesystem::remove_all(TEST_ROOT);
        paths_set_root_path_override(TEST_ROOT);
    }

    ~FakeDataFs() {
        paths_set_root_path_override(nullptr);
        std::filesystem::remove_all(TEST_ROOT);
    }
};

void write_settings(const char* content) {
    std::filesystem::create_directories(std::filesystem::path(SETTINGS_PATH).parent_path());
    FILE* file = std::fopen(SETTINGS_PATH, "w");
    REQUIRE(file != nullptr);
    std::fputs(content, file);
    std::fclose(file);
}

} // namespace

TEST_CASE("radio_region_from_string resolves every region name") {
    for (int i = RADIO_REGION_UNSET; i < RADIO_REGION_COUNT; i++) {
        RadioRegion region = RADIO_REGION_COUNT;
        CHECK_EQ(radio_region_from_string(radio_region_to_string(static_cast<RadioRegion>(i)), &region), ERROR_NONE);
        CHECK_EQ(region, static_cast<RadioRegion>(i));
    }
    RadioRegion region = RADIO_REGION_EU_868;
    CHECK_EQ(radio_region_from_string("MARS_123", &region), ERROR_NOT_FOUND);
    CHECK_EQ(region, RADIO_REGION_EU_868);
}

TEST_CASE("radio_settings_load_region is unset without a settings file") {
    FakeDataFs fake;
    CHECK_EQ(radio_settings_load_region(), RADIO_REGION_UNSET);
}

TEST_CASE("radio_settings_save_region persists the region") {
    FakeDataFs fake;
    CHECK_EQ(radio_settings_save_region(RADIO_REGION_US_915), ERROR_NONE);
    CHECK_EQ(radio_settings_load_region(), RADIO_REGION_US_915);
}

TEST_CASE("radio_settings_load_region reads the existing file format") {
    FakeDataFs fake;
    write_settings("region=EU_868\n");
    CHECK_EQ(radio_settings_load_region(), RADIO_REGION_EU_868);
}

TEST_CASE("radio_settings_load_region ignores an unknown region") {
    FakeDataFs fake;
    write_settings("region=MARS_123\n");
    CHECK_EQ(radio_settings_load_region(), RADIO_REGION_UNSET);
}
