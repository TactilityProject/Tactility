// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/install.h>

TEST_CASE("app_install counts the started installs") {
    const uint32_t before = app_install_get_started_count();
    CHECK_FALSE(app_install_is_active());
    CHECK_NE(app_install("/nonexistent/package.app"), ERROR_NONE);
    CHECK_FALSE(app_install_is_active());
    CHECK_EQ(app_install_get_started_count(), before + 1);
}
