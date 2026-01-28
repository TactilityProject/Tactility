// SPDX-License-Identifier: Apache-2.0
/**
 * @brief Contains various unsorted defines
 * @note Preprocessor defines with potentially clashing names implement an #ifdef check.
 */
#pragma once

#ifndef MIN
/** @brief Get the minimum value of 2 values */
#define MIN(a, b) (a < b ? a : b)
#endif

#ifndef MAX
/** @brief Get the maximum value of 2 values */
#define MAX(a, b) (a > b ? a : b)
#endif

#ifndef CLAMP
/** @brief Clamp a value between the provided minimum and maximum */
#define CLAMP(min, max, value) (value < min) ? min : (value > max ? max : value)
#endif
