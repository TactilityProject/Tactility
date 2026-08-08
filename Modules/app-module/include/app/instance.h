// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identifies a running (or previously running) app instance. 0 is never a valid instance id. */
typedef uint32_t AppInstanceId;

/** Lifecycle state of a running (or previously running) app instance. Every app instance owns
 * its own task for its entire lifetime - there is no "saved, task given up" state. */
typedef enum {
    APP_INSTANCE_STATE_STARTING,
    APP_INSTANCE_STATE_ACTIVE,
    APP_INSTANCE_STATE_STOPPING,
    APP_INSTANCE_STATE_STOPPED,
} AppInstanceState;

#ifdef __cplusplus
}
#endif
