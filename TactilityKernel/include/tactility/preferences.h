// SPDX-License-Identifier: Apache-2.0

/**
 * @brief Key-value settings, persisted as a .properties file on disk (instead of NVS/in-memory).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle - open with preferences_open(), release with preferences_close().
 */
typedef struct Preferences Preferences;

/**
 * Open (or create) a preferences store backed by the properties file at @a path. The parent
 * directory is created (recursively, like mkdir -p) if it doesn't already exist. The file is
 * read into memory now; changes made with preferences_put_*() are only written back to disk by
 * preferences_close().
 * @param[in] path absolute or relative file path (e.g. "/data/settings.properties")
 * @return the new instance, or NULL if the parent directory couldn't be created, or on
 * allocation failure
 */
Preferences* preferences_open(const char* path);

/** Writes any pending preferences_put_*() changes to the backing file, then releases the
 * instance. */
void preferences_close(Preferences* preferences);

bool preferences_has_bool(const Preferences* preferences, const char* key);
bool preferences_has_int32(const Preferences* preferences, const char* key);
bool preferences_has_int64(const Preferences* preferences, const char* key);
bool preferences_has_string(const Preferences* preferences, const char* key);

bool preferences_opt_bool(const Preferences* preferences, const char* key, bool* out_value);
bool preferences_opt_int32(const Preferences* preferences, const char* key, int32_t* out_value);
bool preferences_opt_int64(const Preferences* preferences, const char* key, int64_t* out_value);
/**
 * @retval ERROR_NOT_FOUND @a key is absent or not a string - @a out_value is left untouched
 * @retval ERROR_BUFFER_OVERFLOW out_value_size is too small - @a out_value is left untouched
 * @retval ERROR_NONE on success
 */
error_t preferences_opt_string(const Preferences* preferences, const char* key, char* out_value, size_t out_value_size);

/** Sets the value in the in-memory cache; only persisted to the backing file by
 * preferences_close(). */
void preferences_put_bool(Preferences* preferences, const char* key, bool value);
void preferences_put_int32(Preferences* preferences, const char* key, int32_t value);
void preferences_put_int64(Preferences* preferences, const char* key, int64_t value);
void preferences_put_string(Preferences* preferences, const char* key, const char* value);

#ifdef __cplusplus
}
#endif
