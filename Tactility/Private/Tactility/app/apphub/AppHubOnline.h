// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::app::apphub {

/** False when CONFIG_TT_APP_HUB_ONLINE is disabled: App Hub then only lists the installed apps and uninstalls them */
constexpr bool isOnlineEnabled() {
#if defined(ESP_PLATFORM) && !defined(CONFIG_TT_APP_HUB_ONLINE)
    return false;
#else
    return true;
#endif
}

}
