#pragma once

#include <tactility/concurrent/dispatcher.h>

#include <functional>

namespace tt {

/**
 * A thin C++ wrapper around the TactilityKernel dispatcher, allowing
 * std::function (and therefore capturing lambdas) to be dispatched.
 */
class [[deprecated("Use an app task's stack or a custom task with its own stack")]] MainDispatcher final {

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

/** Provides access to the dispatcher that runs on the main task.
 * @warning This dispatcher is used for WiFi and might block for some time during WiFi connection.
 * @return the dispatcher
 */
[[deprecated("Use an app task's stack or a custom task with its own stack")]]
MainDispatcher getMainDispatcher();

} // namespace tt
