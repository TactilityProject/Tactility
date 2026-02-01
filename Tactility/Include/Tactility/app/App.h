#pragma once

#include "Tactility/app/AppContext.h"

#include <Tactility/Bundle.h>
#include <Tactility/Mutex.h>

#include <string>

// Forward declarations
typedef struct _lv_obj_t lv_obj_t;

namespace tt::app {

// Forward declarations
class AppContext;
enum class Result;

typedef unsigned int LaunchId;

class App {

    Mutex mutex;

    struct ResultHolder {
        Result result;
        std::unique_ptr<Bundle> resultData;

        explicit ResultHolder(Result result) : result(result), resultData(nullptr) {}

        ResultHolder(Result result, std::unique_ptr<Bundle> resultData) :
            result(result),
            resultData(std::move(resultData)) {}
    };

    std::unique_ptr<ResultHolder> resultHolder;

public:

    App() = default;
    virtual ~App() = default;

    virtual void onCreate(AppContext& appContext) {}
    virtual void onDestroy(AppContext& appContext) {}
    virtual void onShow(AppContext& appContext, lv_obj_t* parent) {}
    virtual void onHide(AppContext& appContext) {}
    /** resultData could be null */
    virtual void onResult(AppContext& appContext, LaunchId launchId, Result result, std::unique_ptr<Bundle> resultData) {}

    Mutex& getMutex() { return mutex; }

    bool hasResult() const { return resultHolder != nullptr; }

    void setResult(Result result, std::unique_ptr<Bundle> resultData = nullptr) {
        auto lock = getMutex().asScopedLock();
        lock.lock();
        resultHolder = std::make_unique<ResultHolder>(result, std::move(resultData));
    }

    /**
     * Used by system to extract the result data when this application is finished.
     * Note that this removes the data from the class!
     */
    bool moveResult(Result& outResult, std::unique_ptr<Bundle>& outBundle) {
        auto lock = getMutex().asScopedLock();
        lock.lock();

        if (resultHolder == nullptr) {
            return false;
        }

        outResult = resultHolder->result;
        outBundle = std::move(resultHolder->resultData);
        resultHolder = nullptr;
        return true;
    }
};

template<typename T>
std::shared_ptr<App> create() { return std::shared_ptr<T>(new T); }

/**
 * @brief Start an app
 * @param[in] id application name or id
 * @param[in] parameters optional parameters to pass onto the application. can be nullptr.
 */
LaunchId start(const std::string& id, std::shared_ptr<const Bundle> parameters = nullptr);

/** @brief Stop the currently showing app. Show the previous app if any app was still running. */
void stop();

/** @brief Stop a specific app and any apps it might have launched on the stack.
 * @param[in] id the app id
 */
void stop(const std::string& id);

/** @brief Stop all app instances that match with this identifier and also stop the apps they started.
 * @warning onResult() will only be called for the resulting app that gets shown (if any)
 * @param[in] id the id of the app to stop
 */
void stopAll(const std::string& id);

/** @return true if the app is running somewhere in the app stack (doesn't have to be the top-most app) */
bool isRunning(const std::string& id);

/** @return the currently running app context (it is only ever null before the splash screen is shown) */
std::shared_ptr<AppContext> getCurrentAppContext();

/** @return the currently running app (it is only ever null before the splash screen is shown) */
std::shared_ptr<App> getCurrentApp();

bool install(const std::string& path);

bool uninstall(const std::string& appId);

}
