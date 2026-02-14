// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RootConfig {
    const char* model;
};

/**
 * Indicates whether the device's model matches the specified model.
 * @param[in] device the device to check (non-null)
 * @param[in] model the model to check against
 * @return true if the device's model matches the specified model
 */
bool root_is_model(const struct Device* device, const char* model);

#ifdef __cplusplus
}
#endif
