// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "location.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Character count, excluding null terminator
#define APP_MANIFEST_ID_LENGTH 32

/** Fixed-capacity buffer for an AppManifest::id value - always NUL-terminated. */
typedef char AppId[APP_MANIFEST_ID_LENGTH + 1];

// Character count, excluding null terminator
#define APP_MANIFEST_NAME_LENGTH 32

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
    APP_MANIFEST_FLAG_HIDDEN = 1 << 0,
    /** No window-manager dependency - safe to start from a context with no GUI available. */
    APP_MANIFEST_FLAG_HEADLESS = 1 << 1,
};

/** Largest stack depth (in words) an app may request. Keeps `depth * sizeof(StackType_t)` safely
 * bounded and stops one app from claiming an unreasonable share of available RAM.
 * A depth beyond must be rejected outright.
 * Was last increased because of recursive-descent shell interpreter. */
#define APP_STACK_SIZE_MAX 65535

struct AppStackConfig {
    /** Stack depth (in words, matching FreeRTOS's configSTACK_DEPTH_TYPE) for this app's task.
     * 0 uses the scheduler's default. Must not exceed APP_STACK_SIZE_MAX. */
    uint16_t depth;
    /** Desired memory capability.
     * 0 means default.
     * Combine one or more of \a MemoryCapability from <tactility/memory.h> with a bitwise OR.*/
    uint16_t desired_memory_capability;
};

/** Describes a registrable app. One manifest exists per app id. */
struct AppManifest {
    /** Unique app identifier. Must be NULL-terminated. */
    AppId id;
    /** Human-readable name. Must be NULL-terminated. */
    char name[APP_MANIFEST_NAME_LENGTH + 1];
    enum AppCategory category;
    struct AppLocation location;
    /** Bitmask of AppManifestFlags. Most apps should leave this 0. */
    uint8_t flags;
    /** Stack allocation config for this app's task. */
    struct AppStackConfig stack;
};

bool app_manifest_id_is_valid(const char* id);
bool app_manifest_name_is_valid(const char* name);
bool app_manifest_stack_size_is_valid(const char* value);

#ifdef __cplusplus
}
#endif
