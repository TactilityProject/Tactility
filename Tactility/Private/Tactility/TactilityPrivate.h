#pragma once

namespace tt {

void prepareFileSystems();

/** Starts the kernel modules that Tactility depends on. */
void initModules();

/** Adds the system catalogs and selects the locale of the language setting. Run after initModules(). */
void initLocalisation();

/** Registers and starts the primary system services. */
void registerAndStartServices();

/** Registers the internal apps and the apps installed on mounted file systems. */
void registerApps();

/** Registers and starts the Boot app. */
void startBootApp();

}
