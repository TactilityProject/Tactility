// SPDX-License-Identifier: Apache-2.0

/**
 * @brief key-value storage for general purpose.
 * Maps strings on a fixed set of data types.
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
 * A dictionary that maps keys (strings) onto several atomary types.
 * Opaque handle - allocate with bundle_alloc(), release with bundle_free().
 */
typedef struct Bundle Bundle;

Bundle* bundle_alloc(void);
Bundle* bundle_clone(const Bundle* bundle);
void bundle_free(Bundle* bundle);

/** @warning Undefined if @a key is absent or not a bool - check with bundle_has_bool()/bundle_opt_bool() first. */
bool bundle_get_bool(const Bundle* bundle, const char* key);
/** @warning Undefined if @a key is absent or not an int32 - check with bundle_has_int32()/bundle_opt_int32() first. */
int32_t bundle_get_int32(const Bundle* bundle, const char* key);
/** @warning Undefined if @a key is absent or not an int64 - check with bundle_has_int64()/bundle_opt_int64() first. */
int64_t bundle_get_int64(const Bundle* bundle, const char* key);
/**
 * @warning Undefined if @a key is absent or not a string - check with bundle_has_string()/bundle_opt_string() first.
 * @retval ERROR_BUFFER_OVERFLOW out_value_size is too small
 * @retval ERROR_NONE on success
 */
error_t bundle_get_string(const Bundle* bundle, const char* key, char* out_value, size_t out_value_size);

bool bundle_has_bool(const Bundle* bundle, const char* key);
bool bundle_has_int32(const Bundle* bundle, const char* key);
bool bundle_has_int64(const Bundle* bundle, const char* key);
bool bundle_has_string(const Bundle* bundle, const char* key);

bool bundle_opt_bool(const Bundle* bundle, const char* key, bool* out_value);
bool bundle_opt_int32(const Bundle* bundle, const char* key, int32_t* out_value);
bool bundle_opt_int64(const Bundle* bundle, const char* key, int64_t* out_value);
/**
 * @retval ERROR_NOT_FOUND @a key is absent or not a string - @a out_value is left untouched
 * @retval ERROR_BUFFER_OVERFLOW out_value_size is too small - @a out_value is left untouched
 * @retval ERROR_NONE on success
 */
error_t bundle_opt_string(const Bundle* bundle, const char* key, char* out_value, size_t out_value_size);

void bundle_put_bool(Bundle* bundle, const char* key, bool value);
void bundle_put_int32(Bundle* bundle, const char* key, int32_t value);
void bundle_put_int64(Bundle* bundle, const char* key, int64_t value);
void bundle_put_string(Bundle* bundle, const char* key, const char* value);

#ifdef __cplusplus
}
#endif
