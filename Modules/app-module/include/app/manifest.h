// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "location.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Broad classification of an app, used for grouping/launcher presentation. */
enum AppCategory {
    APP_CATEGORY_SYSTEM,
    APP_CATEGORY_SETTINGS,
    APP_CATEGORY_USER,
};

/** Bit flags for AppManifest::flags. */
enum AppManifestFlags {
    /** Excluded from generic app-browsing UIs (AppList, Settings) - for apps only ever reached
     * by direct navigation (modal dialogs, detail views that require parameters, wizard/
     * bootstrap steps). */
    APP_MANIFEST_FLAG_HIDDEN = 0b00000001,
};

/** Describes a registrable app. One manifest exists per app id. */
struct AppManifest {
    /** Unique app identifier. Should never be NULL. */
    const char* id;
    /** Human-readable name. Should never be NULL. */
    const char* name;
    enum AppCategory category;
    struct AppLocation location;
    /** Bitmask of AppManifestFlags. Most apps should leave this 0. */
    uint8_t flags;
};

#ifdef __cplusplus
}
#endif
