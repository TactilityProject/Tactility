#include <Tactility/service/loader/Loader.h>
#include <Tactility/app/AppInstance.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/AppRegistration.h>

#include <Tactility/LogMessages.h>
#include <Tactility/DispatcherThread.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <vector>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <utility>
#endif

#include <tactility/log.h>

namespace tt::service::loader {

constexpr auto* TAG = "Loader";

constexpr auto LOADER_TIMEOUT = (100 / portTICK_PERIOD_MS);

// Forward declaration
extern const ServiceManifest manifest;

static const char* appStateToString(app::State state) {
    switch (state) {
        using enum app::State;
        case Initial:
            return "initial";
        case Created:
            return "started";
        case Showing:
            return "showing";
        case Hiding:
            return "hiding";
        case Destroyed:
            return "stopped";
        default:
            return "?";
    }
}

void LoaderService::onStartAppMessage(const std::string& id, app::LaunchId launchId, std::shared_ptr<const Bundle> parameters) {
    LOG_I(TAG, "Start by id %s", id.c_str());

    auto app_manifest = app::findAppManifestById(id);
    if (app_manifest == nullptr) {
        LOG_E(TAG, "App not found: %s", id.c_str());
        return;
    }

    auto lock = mutex.asScopedLock();
    if (!lock.lock(LOADER_TIMEOUT)) {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    auto previous_app = !appStack.empty() ? appStack[appStack.size() - 1]: nullptr;
    auto new_app = std::make_shared<app::AppInstance>(app_manifest, launchId, parameters);

    new_app->mutableFlags().hideStatusbar = (app_manifest->appFlags & app::AppManifest::Flags::HideStatusBar);

    // We might have to hide the previous app first
    if (previous_app != nullptr) {
        transitionAppToState(previous_app, app::State::Hiding);
    }

    appStack.push_back(new_app);
    transitionAppToState(new_app, app::State::Created);
    transitionAppToState(new_app, app::State::Showing);
}

void LoaderService::onStopTopAppMessage(const std::string& id) {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(LOADER_TIMEOUT)) {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    size_t original_stack_size = appStack.size();

    if (original_stack_size == 0) {
        LOG_E(TAG, "Stop app: no app running");
        return;
    }

    // Stop current app
    auto app_to_stop = appStack[appStack.size() - 1];

    if (app_to_stop->getManifest().appId != id) {
        LOG_E(TAG, "Stop app: id mismatch (wanted %s but found %s on top of stack)", id.c_str(), app_to_stop->getManifest().appId.c_str());
        return;
    }

    if (original_stack_size == 1 && app_to_stop->getManifest().appName != "Boot") {
        LOG_E(TAG, "Stop app: can't stop root app");
        return;
    }

    bool result_set = false;
    app::Result result;
    std::unique_ptr<Bundle> result_bundle;
    if (app_to_stop->getApp()->moveResult(result, result_bundle)) {
        result_set = true;
    }

    auto app_to_stop_launch_id = app_to_stop->getLaunchId();

    transitionAppToState(app_to_stop, app::State::Hiding);
    transitionAppToState(app_to_stop, app::State::Destroyed);

    appStack.pop_back();

    // We only expect the app to be referenced within the current scope
    if (app_to_stop.use_count() > 1) {
        LOG_W(TAG, "Memory leak: Stopped %s, but use count is %d", app_to_stop->getManifest().appId.c_str(), (int)(app_to_stop.use_count() - 1));
    }

    // Refcount is expected to be 2: 1 within app_to_stop and 1 within the current scope
    if (app_to_stop->getApp().use_count() > 2) {
        LOG_W(TAG, "Memory leak: Stopped %s, but use count is %d", app_to_stop->getManifest().appId.c_str(), (int)(app_to_stop->getApp().use_count() - 2));
    }

#ifdef ESP_PLATFORM
    LOG_I(TAG, "Free heap: %d", (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif

    std::shared_ptr<app::AppInstance> instance_to_resume;
    // If there's a previous app, resume it
    if (!appStack.empty()) {
        instance_to_resume = appStack[appStack.size() - 1];
        assert(instance_to_resume);
        transitionAppToState(instance_to_resume, app::State::Showing);
    }

    // Unlock so that we can send results to app and they can also start/stop new apps while processing these results
    lock.unlock();
    // WARNING: After this point we cannot change the app states from this method directly anymore as we don't have a lock!

    if (instance_to_resume != nullptr) {
        if (result_set) {
            if (result_bundle != nullptr) {
                instance_to_resume->getApp()->onResult(
                    *instance_to_resume,
                    app_to_stop_launch_id,
                    result,
                    std::move(result_bundle)
                );
            } else {
                instance_to_resume->getApp()->onResult(
                    *instance_to_resume,
                    app_to_stop_launch_id,
                    result,
                    nullptr
                );
            }
        } else {
            instance_to_resume->getApp()->onResult(
                *instance_to_resume,
                app_to_stop_launch_id,
                app::Result::Cancelled,
                nullptr
            );
        }
    }
}

int LoaderService::findAppInStack(const std::string& id) const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    for (size_t i = 0; i < appStack.size(); i++) {
        if (appStack[i]->getManifest().appId == id) {
            return i;
        }
    }
    return -1;
}

void LoaderService::onStopAllAppMessage(const std::string& id) {
    auto lock = mutex.asScopedLock();
    if (!lock.lock(LOADER_TIMEOUT)) {
        LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    if (!isRunning(id)) {
        LOG_E(TAG, "Stop all: %s not running", id.c_str());
        return;
    }

    int app_to_stop_index = findAppInStack(id);
    if (app_to_stop_index < 0) {
        LOG_E(TAG, "Stop all: %s not found in stack", id.c_str());
        return;
    }


    // Find an app to resume, if any
    std::shared_ptr<app::AppInstance> instance_to_resume;
    if (app_to_stop_index > 0) {
        instance_to_resume = appStack[app_to_stop_index - 1];
        assert(instance_to_resume);
    }

    // Stop all apps and find the LaunchId of the last-closed app, so we can call onResult() if needed
    app::LaunchId last_launch_id = 0;
    for (int i = appStack.size() - 1; i >= app_to_stop_index; i--) {
        auto app_to_stop = appStack[i];
        // Hide the app first in case it's still being shown
        if (app_to_stop->getState() == app::State::Showing) {
            transitionAppToState(app_to_stop, app::State::Hiding);
        }
        transitionAppToState(app_to_stop, app::State::Destroyed);
        last_launch_id = app_to_stop->getLaunchId();

        appStack.pop_back();
    }

    if (instance_to_resume != nullptr) {
        LOG_I(TAG, "Resuming %s", instance_to_resume->getManifest().appId.c_str());
        transitionAppToState(instance_to_resume, app::State::Showing);

        instance_to_resume->getApp()->onResult(
            *instance_to_resume,
            last_launch_id,
            app::Result::Cancelled,
            nullptr
        );
    }
}

void LoaderService::transitionAppToState(const std::shared_ptr<app::AppInstance>& app, app::State state) {
    const app::AppManifest& app_manifest = app->getManifest();
    const app::State old_state = app->getState();

    LOG_I(TAG, "App \"%s\" state: %s -> %s",
        app_manifest.appId.c_str(),
        appStateToString(old_state),
        appStateToString(state)
    );

    switch (state) {
        using enum app::State;
        case Initial:
            check(false, LOG_MESSAGE_ILLEGAL_STATE);
        case Created:
            assert(app->getState() == app::State::Initial);
            app->getApp()->onCreate(*app);
            pubsubExternal->publish(Event::ApplicationStarted);
            break;
        case Showing: {
            assert(app->getState() == app::State::Hiding || app->getState() == app::State::Created);
            pubsubExternal->publish(Event::ApplicationShowing);
            break;
        }
        case Hiding: {
            assert(app->getState() == app::State::Showing);
            pubsubExternal->publish(Event::ApplicationHiding);
            break;
        }
        case Destroyed:
            app->getApp()->onDestroy(*app);
            pubsubExternal->publish(Event::ApplicationStopped);
            break;
    }

    app->setState(state);
}

app::LaunchId LoaderService::start(const std::string& id, std::shared_ptr<const Bundle> parameters) {
    const auto launch_id = nextLaunchId++;
    dispatcherThread->dispatch([this, id, launch_id, parameters]() {
        onStartAppMessage(id, launch_id, parameters);
    });
    return launch_id;
}

void LoaderService::stopTop() {
    const auto& id = getCurrentAppContext()->getManifest().appId;
    stopTop(id);
}

void LoaderService::stopTop(const std::string& id) {
    LOG_I(TAG, "dispatching stopTop(%s)", id.c_str());
    dispatcherThread->dispatch([this, id] {
        onStopTopAppMessage(id);
    });
}

void LoaderService::stopAll(const std::string& id) {
    LOG_I(TAG, "dispatching stopAll(%s)", id.c_str());
    dispatcherThread->dispatch([this, id] {
        onStopAllAppMessage(id);
    });
}

std::shared_ptr<app::AppContext> LoaderService::getCurrentAppContext() {
    const auto lock = mutex.asScopedLock();
    lock.lock();
    if (appStack.empty()) {
        return nullptr;
    } else {
        return appStack[appStack.size() - 1];
    }
}

bool LoaderService::isRunning(const std::string& id) const {
    const auto lock = mutex.asScopedLock();
    lock.lock();
    for (const auto& app : appStack) {
        if (app->getManifest().appId == id) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<LoaderService> findLoaderService() {
    return service::findServiceById<LoaderService>(manifest.id);
}

extern const ServiceManifest manifest = {
    .id = "Loader",
    .createService = create<LoaderService>
};


} // namespace
