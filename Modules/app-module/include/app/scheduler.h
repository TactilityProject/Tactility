// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @return the app_instance_id of whichever app instance's task is calling this (every app
 * instance's task stashes it in its own thread-local storage when it starts), or 0 if called
 * from a task that isn't a running app instance. An app's own main() typically calls this once,
 * near the top, to learn its own instance id - see e.g. app_event_subscribe()/
 * window_manager_create(), both of which need it.
 */
AppInstanceId app_scheduler_current_app_id(void);

#ifdef __cplusplus
}
#endif
