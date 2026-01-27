#pragma once

#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/ElfApp.h>

#include <Tactility/Bundle.h>
#include <tactility/check.h>
#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>

#include <memory>
#include <utility>

namespace tt::app {

enum class State {
    Initial, // AppInstance was created, but the state hasn't advanced yet
    Created, // App was placed into memory
    Showing, // App view was created
    Hiding,  // App view was destroyed
    Destroyed  // App was removed from memory
};

/**
 * Thread-safe app instance.
 */
class AppInstance : public AppContext {

    Mutex mutex;
    const std::shared_ptr<AppManifest> manifest;
    State state = State::Initial;
    LaunchId launchId;
    Flags flags = { .hideStatusbar = true };
    /** @brief Optional parameters to start the app with
     * When these are stored in the app struct, the struct takes ownership.
     * Do not mutate after app creation.
     */
    std::shared_ptr<const Bundle> _Nullable parameters;

    std::shared_ptr<App> app;

    static std::shared_ptr<App> createApp(
        const std::shared_ptr<AppManifest>& manifest
    ) {
        if (manifest->appLocation.isInternal()) {
            assert(manifest->createApp != nullptr);
            return manifest->createApp();
        } else if (manifest->appLocation.isExternal()) {
            if (manifest->createApp != nullptr) {
                Logger("AppInstance").warn("Manifest specifies createApp, but this is not used with external apps");
            }
#ifdef ESP_PLATFORM
            return createElfApp(manifest);
#else
            check(false, "not supported");
#endif
        } else {
            check(false, "not implemented");
        }
    }

public:

    explicit AppInstance(const std::shared_ptr<AppManifest>& manifest, LaunchId launchId) :
        manifest(manifest),
        launchId(launchId),
        app(createApp(manifest))
    {}

    AppInstance(const std::shared_ptr<AppManifest>& manifest, LaunchId launchId, std::shared_ptr<const Bundle> parameters) :
        manifest(manifest),
        launchId(launchId),
        parameters(std::move(parameters)),
        app(createApp(manifest))
    {}

    ~AppInstance() override = default;

    LaunchId getLaunchId() const { return launchId; }

    void setState(State state);
    State getState() const;

    const AppManifest& getManifest() const override;

    Flags getFlags() const;
    void setFlags(Flags flags);
    Flags& mutableFlags() { return flags; } // TODO: locking mechanism

    std::shared_ptr<const Bundle> getParameters() const override;

    std::unique_ptr<AppPaths> getPaths() const override;

    std::shared_ptr<App> getApp() const override { return app; }
};

} // namespace
