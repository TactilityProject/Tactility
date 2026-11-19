#include "doctest.h"

#include <Tactility/settings/StandbySettings.h>

using namespace tt::settings::standby;

TEST_CASE("defaults keep only mesh awake") {
    const auto settings = getDefault();
    CHECK_FALSE(settings.keepDisplay);
    CHECK_FALSE(settings.keepWifi);
    CHECK_FALSE(settings.keepBluetooth);
    CHECK(settings.keepMesh);
    CHECK(settings.wake.empty());
}

TEST_CASE("buttons don't wake the device by default") {
    CHECK(isWakeDefault(WakeKind::Keyboard));
    CHECK(isWakeDefault(WakeKind::Touch));
    CHECK(isWakeDefault(WakeKind::Encoder));
    CHECK_FALSE(isWakeDefault(WakeKind::Button));
}

TEST_CASE("a wake choice of a device overrides the default of its kind") {
    auto settings = getDefault();
    settings.wake["keyboard0"] = false;
    settings.wake["button0"] = true;
    CHECK_FALSE(isWakeEnabled(settings, "keyboard0", WakeKind::Keyboard));
    CHECK(isWakeEnabled(settings, "button0", WakeKind::Button));
    CHECK(isWakeEnabled(settings, "touch0", WakeKind::Touch));
    CHECK_FALSE(isWakeEnabled(settings, "button1", WakeKind::Button));
}

TEST_CASE("standby settings are saved and loaded") {
    StandbySettings settings = getDefault();
    settings.keepDisplay = true;
    settings.keepWifi = true;
    settings.keepMesh = false;
    settings.wake["encoder0"] = false;
    settings.wake["button0"] = true;
    REQUIRE(save(settings));

    StandbySettings loaded;
    REQUIRE(load(loaded));
    CHECK(loaded.keepDisplay);
    CHECK(loaded.keepWifi);
    CHECK_FALSE(loaded.keepBluetooth);
    CHECK_FALSE(loaded.keepMesh);
    CHECK_EQ(loaded.wake.size(), 2);
    CHECK_FALSE(isWakeEnabled(loaded, "encoder0", WakeKind::Encoder));
    CHECK(isWakeEnabled(loaded, "button0", WakeKind::Button));

    REQUIRE(save(getDefault()));
}
