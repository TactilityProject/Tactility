// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum AppLocationType {
    APP_LOCATION_MEMORY,
    APP_LOCATION_PATH,
};

struct AppLocation {
    enum AppLocationType type;
    /** Meaning depends on `type`; see AppLocationType. */
    void* location;
};

#ifdef __cplusplus
}
#endif
