#include "doctest.h"
#include <Tactility/settings/LauncherSettings.h>

using namespace tt::settings::launcher;

static constexpr SystemBarsCapabilities ANY = { SystemBarsCapability::Any, SystemBarsCapability::Any };

TEST_CASE("auto system bars use the side layout only for wide landscape screens") {
    const LauncherSettings settings = getDefault();
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 320, 240), SystemBarsLayout::Split);
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 480, 222), SystemBarsLayout::Side);
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 240, 320), SystemBarsLayout::Split);
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 222, 480), SystemBarsLayout::Split);
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 300, 200), SystemBarsLayout::Side);
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 240, 240), SystemBarsLayout::Split);
}

TEST_CASE("custom system bars use the layout of the screen's orientation") {
    LauncherSettings settings = getDefault();
    settings.systemBarsMode = SystemBarsMode::Custom;
    settings.portraitLayout = SystemBarsLayout::Side;
    settings.landscapeLayout = SystemBarsLayout::Split;
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 240, 320), SystemBarsLayout::Side);
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 480, 222), SystemBarsLayout::Split);
    // A square screen counts as portrait
    CHECK_EQ(resolveSystemBarsLayout(settings, ANY, 240, 240), SystemBarsLayout::Side);
}

TEST_CASE("a fixed capability decides the layout instead of the settings") {
    const SystemBarsCapabilities fixed = { SystemBarsCapability::Side, SystemBarsCapability::Split };
    LauncherSettings settings = getDefault();
    CHECK_EQ(resolveSystemBarsLayout(settings, fixed, 240, 320), SystemBarsLayout::Side);
    CHECK_EQ(resolveSystemBarsLayout(settings, fixed, 480, 222), SystemBarsLayout::Split);
    settings.systemBarsMode = SystemBarsMode::Custom;
    settings.portraitLayout = SystemBarsLayout::Split;
    settings.landscapeLayout = SystemBarsLayout::Side;
    CHECK_EQ(resolveSystemBarsLayout(settings, fixed, 240, 320), SystemBarsLayout::Side);
    CHECK_EQ(resolveSystemBarsLayout(settings, fixed, 480, 222), SystemBarsLayout::Split);
}

TEST_CASE("an orientation with the Any capability follows the settings while the other is fixed") {
    const SystemBarsCapabilities landscape_fixed = { SystemBarsCapability::Any, SystemBarsCapability::Split };
    LauncherSettings settings = getDefault();
    settings.systemBarsMode = SystemBarsMode::Custom;
    settings.portraitLayout = SystemBarsLayout::Side;
    settings.landscapeLayout = SystemBarsLayout::Side;
    CHECK_EQ(resolveSystemBarsLayout(settings, landscape_fixed, 240, 320), SystemBarsLayout::Side);
    CHECK_EQ(resolveSystemBarsLayout(settings, landscape_fixed, 480, 222), SystemBarsLayout::Split);
}
