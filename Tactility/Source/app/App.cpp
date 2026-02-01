#include <Tactility/app/App.h>
#include <Tactility/service/loader/Loader.h>

namespace tt::app {

constexpr auto* TAG = "App";

LaunchId start(const std::string& id, std::shared_ptr<const Bundle> parameters) {
    const auto service = service::loader::findLoaderService();
    assert(service != nullptr);
    return service->start(id, std::move(parameters));
}

void stop() {
    const auto service = service::loader::findLoaderService();
    assert(service != nullptr);
    service->stopTop();
}

void stop(const std::string& id) {
    const auto service = service::loader::findLoaderService();
    assert(service != nullptr);
    service->stopTop(id);
}

void stopAll(const std::string& id) {
    const auto service = service::loader::findLoaderService();
    assert(service != nullptr);
    service->stopAll(id);
}

bool isRunning(const std::string& id) {
    const auto service = service::loader::findLoaderService();
    assert(service != nullptr);
    return service->isRunning(id);
}

std::shared_ptr<AppContext> getCurrentAppContext() {
    const auto service = service::loader::findLoaderService();
    assert(service != nullptr);
    return service->getCurrentAppContext();
}

std::shared_ptr<App> getCurrentApp() {
    const auto app_context = getCurrentAppContext();
    return (app_context != nullptr) ? app_context->getApp() : nullptr;
}

}
