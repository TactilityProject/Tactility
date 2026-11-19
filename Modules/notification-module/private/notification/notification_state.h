// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

/** Allocates the history. Called when the module starts. */
error_t notification_state_start(void);

/** Frees the history and its data. Called when the module stops. */
void notification_state_stop(void);
