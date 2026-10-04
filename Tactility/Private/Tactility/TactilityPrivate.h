#pragma once

#include <tactility/concurrent/dispatcher.h>

namespace tt {

/** @return the handle of the dispatcher that getMainDispatcher() wraps */
DispatcherHandle_t getMainDispatcherHandle();

void prepareFileSystems();

/** Starts the kernel modules that Tactility depends on. */
void initModules();

/** Registers and starts the primary system services. */
void registerAndStartServices();

/** Registers the internal apps and the apps installed on mounted file systems. */
void registerApps();

/** Registers and starts the Boot app. */
void startBootApp();

}
