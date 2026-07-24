#pragma once

#include <tactility/concurrent/dispatcher.h>
#include <tactility/device.h>
#include <tactility/module.h>
#include <Tactility/app/AppManifest.h>

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

/**
 * @brief Main entry point for Tactility.
 * @param dtsModules List of modules from devicetree, null-terminated, non-null parameter
 * @param dtsDevices Array that is terminated with DTS_DEVICE_TERMINATOR
 */
void run(Module* dtsModules[], DtsDevice dtsDevices[]);

/** Provides access to the dispatcher that runs on the main task.
 * @warning This dispatcher is used for WiFi and might block for some time during WiFi connection.
 * @return the dispatcher
 */
MainDispatcher getMainDispatcher();

} // namespace tt
