#pragma once

#include "tactility/concurrent/dispatcher.h"
#include "tactility/device.h"
#include "tactility/module.h"
#include <Tactility/app/AppManifest.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/service/ServiceManifest.h>

#include <functional>

extern "C" {
struct DtsDevice;
}

namespace tt {

/**
 * A thin C++ wrapper around the TactilityKernel dispatcher, allowing
 * std::function (and therefore capturing lambdas) to be dispatched.
 */
class MainDispatcher final {

public:

    using Function = std::function<void()>;

    explicit MainDispatcher(DispatcherHandle_t handle) : handle(handle) {}

    /**
     * Queue a function to be consumed on the main task.
     * @param[in] function the function to execute elsewhere
     * @param[in] timeout lock acquisition timeout
     * @return true if dispatching was successful (timeout not reached)
     */
    bool dispatch(Function function, TickType_t timeout = portMAX_DELAY) const;

private:

    DispatcherHandle_t handle;
};

/** @brief The configuration for the operating system
 * It contains the hardware configuration, apps and services
 */
struct Configuration {
    /** HAL configuration (drivers) */
    const hal::Configuration* hardware = nullptr;
};

/**
 * @brief Main entry point for Tactility.
 * @param dtsModules List of modules from devicetree, null-terminated, non-null parameter
 * @param dtsDevices Array that is terminated with DTS_DEVICE_TERMINATOR
 */
void run(const Configuration& config, Module* dtsModules[], DtsDevice dtsDevices[]);

/**
 * While technically nullable, this instance is always set if tt_init() succeeds.
 * Could return nullptr if init was not called.
 * @return the Configuration instance that was passed on to tt_init() if init is successful
 */
const Configuration* getConfiguration();

/** Provides access to the dispatcher that runs on the main task.
 * @warning This dispatcher is used for WiFi and might block for some time during WiFi connection.
 * @return the dispatcher
 */
MainDispatcher getMainDispatcher();

namespace hal {

/** While technically this configuration is nullable, it's never null after initHeadless() is called. */
const Configuration* getConfiguration();

} // namespace hal

} // namespace tt
