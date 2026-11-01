// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/install.h>

TEST_CASE("app_install_is_active is false after a failed app_install") {
    CHECK_FALSE(app_install_is_active());
    CHECK_NE(app_install("/nonexistent/package.app"), ERROR_NONE);
    CHECK_FALSE(app_install_is_active());
}
