#pragma once

#include "tactility/device.h"
#include "tactility/module.h"
#include <Tactility/Dispatcher.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/service/ServiceManifest.h>

namespace tt {

/** @brief The configuration for the operating system
 * It contains the hardware configuration, apps and services
 */
struct Configuration {
    /** HAL configuration (drivers) */
    const hal::Configuration* hardware = nullptr;
};

/**
 * Attempts to initialize Tactility and all configured hardware.
 */
void run(const Configuration& config, Module* platformModule, Module* deviceModule, CompatibleDevice devicetreeDevices[]);

/**
 * While technically nullable, this instance is always set if tt_init() succeeds.
 * @return the Configuration instance that was passed on to tt_init() if init is successful
 */
const Configuration* _Nullable getConfiguration();

/** Provides access to the dispatcher that runs on the main task.
 * @warning This dispatcher is used for WiFi and might block for some time during WiFi connection.
 * @return the dispatcher
 */
Dispatcher& getMainDispatcher();

namespace hal {

/** While technically this configuration is nullable, it's never null after initHeadless() is called. */
const Configuration* _Nullable getConfiguration();

} // namespace hal

} // namespace tt
