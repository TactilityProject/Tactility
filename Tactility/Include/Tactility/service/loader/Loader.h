#pragma once

#include <Tactility/DispatcherThread.h>
#include <Tactility/Bundle.h>
#include <Tactility/PubSub.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/app/AppInstance.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/service/Service.h>

#include <memory>

namespace tt::service::loader {


class LoaderService final : public Service {

public:

    enum class Event {
        ApplicationStarted,
        ApplicationShowing,
        ApplicationHiding,
        ApplicationStopped
    };

private:

    std::shared_ptr<PubSub<Event>> pubsubExternal = std::make_shared<PubSub<Event>>();
    RecursiveMutex mutex;
    std::vector<std::shared_ptr<app::AppInstance>> appStack;
    app::LaunchId nextLaunchId = 0;

    /** The dispatcher thread needs a callstack large enough to accommodate all the dispatched methods.
     * This includes full LVGL redraw via Gui::redraw()
     */
    std::unique_ptr<DispatcherThread> dispatcherThread = std::make_unique<DispatcherThread>("loader_dispatcher", 6144); // Files app requires ~5k

    void onStartAppMessage(const std::string& id, app::LaunchId launchId, std::shared_ptr<const Bundle> parameters);

    void onStopTopAppMessage(const std::string& id);

    void onStopAllAppMessage(const std::string& id);

    void transitionAppToState(const std::shared_ptr<app::AppInstance>& app, app::State state);

    int findAppInStack(const std::string& id) const;

    bool onStart(ServiceContext& service) override {
        dispatcherThread->start();
        return true;
    }

    void onStop(ServiceContext& service) override {
        // Send stop signal to thread and wait for thread to finish
        mutex.withLock([this] {
            dispatcherThread->stop();
        });
    }

public:
    /**
     * @brief Start an app given an app id and an optional bundle with parameters
     * @param id the app identifier
     * @param parameters optional parameter bundle (nullable)
     * @return the launch id
     */
    app::LaunchId start(const std::string& id, std::shared_ptr<const Bundle> parameters);

    /**
     * @brief Stops the top-most app (the one that is currently active shown to the user
     * @warning Avoid calling this directly and use stopTop(id) instead
     */
    void stopTop();

    /**
     * @brief Stops the top-most app if the id is still matching by the time the stop event arrives.
     * @param id the id of the app to stop
     */
    void stopTop(const std::string& id);

    /**
     * @brief Stops all apps with the provided id and any apps that were pushed on top of the stack after the original app was started.
     * @param id the id of the app to stop
     */
    void stopAll(const std::string& id);

    /** @return the AppContext of the top-most application, or nullptr if no app is running. */
    std::shared_ptr<app::AppContext> getCurrentAppContext();

    /** @return true if the app is running anywhere in the app stack (the app does not have to be the top-most one for this to return true) */
    bool isRunning(const std::string& id) const;

    /** @return the PubSub object that is responsible for event publishing */
    std::shared_ptr<PubSub<Event>> getPubsub() const { return pubsubExternal; }
};

/** return the service or nullptr if it's not running */
std::shared_ptr<LoaderService> findLoaderService();

} // namespace
