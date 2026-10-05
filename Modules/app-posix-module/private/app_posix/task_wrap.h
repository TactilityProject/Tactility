// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifndef __APPLE__

/** Routes the tasks and threads an app creates through resource tracking (see app/resources.h). Idempotent. */
void app_posix_install_task_hooks();

#else

// Resources aren't tracked on Apple platforms
inline void app_posix_install_task_hooks() {}

#endif
