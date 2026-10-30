#include "doctest.h"

#include <Tactility/file/File.h>
#include <Tactility/settings/AppearanceSettings.h>

#include <app/paths.h>
#include <tactility/error.h>

using namespace tt::settings::appearance;

namespace {

std::string getSettingsPath() {
    char path[256];
    REQUIRE(app_paths_get_user_data_path("tactility.appearance", "appearance.properties", path, sizeof(path)) == ERROR_NONE);
    return path;
}

void writeSettingsFile(const std::string& content) {
    const auto path = getSettingsPath();
    REQUIRE(tt::file::findOrCreateParentDirectory(path, 0755));
    REQUIRE(tt::file::writeString(path, content));
}

}

TEST_CASE("spacing and size are saved and loaded") {
    AppearanceSettings settings;
    settings.spacing = 40;
    settings.size = 80;
    REQUIRE(save(settings));

    AppearanceSettings loaded;
    REQUIRE(load(loaded));
    CHECK_EQ(loaded.spacing, std::optional<uint8_t>(40));
    CHECK_EQ(loaded.size, std::optional<uint8_t>(80));
}

TEST_CASE("unset spacing and size stay unset after saving over set values") {
    AppearanceSettings settings;
    settings.spacing = 20;
    settings.size = 20;
    REQUIRE(save(settings));

    settings.spacing = std::nullopt;
    settings.size = std::nullopt;
    REQUIRE(save(settings));

    AppearanceSettings loaded;
    REQUIRE(load(loaded));
    CHECK_FALSE(loaded.spacing.has_value());
    CHECK_FALSE(loaded.size.has_value());
}

TEST_CASE("the old density setting becomes spacing and size") {
    writeSettingsFile("density=compact\n");
    AppearanceSettings loaded;
    REQUIRE(load(loaded));
    CHECK_EQ(loaded.spacing, std::optional<uint8_t>(0));
    CHECK_EQ(loaded.size, std::optional<uint8_t>(0));

    writeSettingsFile("density=regular\n");
    REQUIRE(load(loaded));
    CHECK_EQ(loaded.spacing, std::optional<uint8_t>(100));
    CHECK_EQ(loaded.size, std::optional<uint8_t>(100));

    writeSettingsFile("density=default\n");
    REQUIRE(load(loaded));
    CHECK_FALSE(loaded.spacing.has_value());
    CHECK_FALSE(loaded.size.has_value());
}

TEST_CASE("spacing and size take precedence over the old density setting") {
    writeSettingsFile("density=compact\nspacing=60\nsize=\n");
    AppearanceSettings loaded;
    REQUIRE(load(loaded));
    CHECK_EQ(loaded.spacing, std::optional<uint8_t>(60));
    CHECK_EQ(loaded.size, std::optional<uint8_t>(0));
}

TEST_CASE("the old density setting is not applied again after saving the device defaults") {
    writeSettingsFile("density=compact\n");
    AppearanceSettings settings;
    REQUIRE(load(settings));
    settings.spacing = std::nullopt;
    settings.size = std::nullopt;
    REQUIRE(save(settings));

    AppearanceSettings loaded;
    REQUIRE(load(loaded));
    CHECK_FALSE(loaded.spacing.has_value());
    CHECK_FALSE(loaded.size.has_value());
}

TEST_CASE("invalid spacing and size count as unset") {
    writeSettingsFile("spacing=150\nsize=abc\n");
    AppearanceSettings loaded;
    REQUIRE(load(loaded));
    CHECK_FALSE(loaded.spacing.has_value());
    CHECK_FALSE(loaded.size.has_value());
}
