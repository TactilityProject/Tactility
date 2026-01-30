// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Avoid potential clash with bits/types/error_t.h
#ifndef __error_t_defined
typedef int error_t;
#endif

#define ERROR_NONE 0
#define ERROR_UNDEFINED 1
#define ERROR_INVALID_STATE 2
#define ERROR_INVALID_ARGUMENT 3
#define ERROR_MISSING_PARAMETER 4
#define ERROR_NOT_FOUND 5
#define ERROR_ISR_STATUS 6
#define ERROR_RESOURCE 7 // A problem with a resource/dependency
#define ERROR_TIMEOUT 8
#define ERROR_OUT_OF_MEMORY 9
#define ERROR_NOT_SUPPORTED 10
#define ERROR_NOT_ALLOWED 11

/** Convert an error_t to a human-readable text. Useful for logging. */
const char* error_to_string(error_t error);

#ifdef __cplusplus
}
#endif
