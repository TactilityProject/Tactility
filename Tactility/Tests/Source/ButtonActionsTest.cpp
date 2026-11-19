#include "doctest.h"

#include <Tactility/service/buttonactions/ButtonActions.h>

#include <tactility/drivers/input_codes.h>

using namespace tt::service::buttonactions;

TEST_CASE("power, standby and suspend keys trigger their actions") {
    CHECK(getAction(BUTTON_KEY_POWER) == Action::PowerMenu);
    CHECK(getAction(BUTTON_KEY_STANDBY) == Action::Standby);
    CHECK(getAction(BUTTON_KEY_SUSPEND) == Action::Suspend);
}

TEST_CASE("other keys trigger nothing") {
    CHECK(getAction(BUTTON_KEY_NONE) == Action::None);
    CHECK(getAction(BUTTON_KEY_ENTER) == Action::None);
    CHECK(getAction(BUTTON_KEY_BACK) == Action::None);
}
