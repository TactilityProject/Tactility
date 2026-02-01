#include "tt_app.h"
#include <Tactility/app/App.h>
#include <Tactility/app/AppPaths.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/app/ElfApp.h>
#include <Tactility/Logger.h>

#include <cstring>

static const auto LOGGER = tt::Logger("tt_app");

extern "C" {

#define HANDLE_AS_APP_CONTEXT(handle) ((tt::app::AppContext*)(handle))

void tt_app_register(
    const AppRegistration appRegistration
) {
#ifdef ESP_PLATFORM
    assert((appRegistration.createData == nullptr) == (appRegistration.destroyData == nullptr));
    tt::app::setElfAppParameters(
        appRegistration.createData,
        appRegistration.destroyData,
        appRegistration.onCreate,
        appRegistration.onDestroy,
        appRegistration.onShow,
        appRegistration.onHide,
        reinterpret_cast<tt::app::OnResult>(appRegistration.onResult)
    );
#else
    check(false, "TactilityC is not intended for PC/Simulator");
#endif
}

BundleHandle tt_app_get_parameters(AppHandle handle) {
    return (BundleHandle)HANDLE_AS_APP_CONTEXT(handle)->getParameters().get();
}

void tt_app_set_result(AppHandle handle, AppResult result, BundleHandle bundle) {
    auto shared_bundle = std::unique_ptr<tt::Bundle>(static_cast<tt::Bundle*>(bundle));
    HANDLE_AS_APP_CONTEXT(handle)->getApp()->setResult(static_cast<tt::app::Result>(result), std::move(shared_bundle));
}

bool tt_app_has_result(AppHandle handle) {
    return HANDLE_AS_APP_CONTEXT(handle)->getApp()->hasResult();
}

void tt_app_start(const char* appId) {
    tt::app::start(appId);
}

void tt_app_start_with_bundle(const char* appId, BundleHandle parameters) {
    tt::app::start(appId, std::shared_ptr<tt::Bundle>(static_cast<tt::Bundle*>(parameters)));
}

void tt_app_stop() {
    tt::app::stop();
}

void tt_app_get_user_data_path(AppHandle handle, char* buffer, size_t* size) {
    assert(buffer != nullptr);
    assert(size != nullptr);
    assert(*size > 0);
    const auto paths = HANDLE_AS_APP_CONTEXT(handle)->getPaths();
    const auto data_path = paths->getUserDataPath();
    const auto expected_length = data_path.length() + 1;
    if (*size < expected_length) {
        LOGGER.error("Path buffer not large enough ({} < {})", *size, expected_length);
        *size = 0;
        buffer[0] = 0;
        return;
    }

    strcpy(buffer, data_path.c_str());
    *size = data_path.length();
}

void tt_app_get_user_data_child_path(AppHandle handle, const char* childPath, char* buffer, size_t* size) {
    assert(buffer != nullptr);
    assert(size != nullptr);
    assert(*size > 0);
    const auto paths = HANDLE_AS_APP_CONTEXT(handle)->getPaths();
    const auto resolved_path = paths->getUserDataPath(childPath);
    const auto resolved_path_length = resolved_path.length();
    if (*size < (resolved_path_length + 1)) {
        LOGGER.error("Path buffer not large enough ({} < {})", *size, (resolved_path_length + 1));
        *size = 0;
        buffer[0] = 0;
        return;
    }

    strcpy(buffer, resolved_path.c_str());
    *size = resolved_path_length;
}

void tt_app_get_assets_path(AppHandle handle, char* buffer, size_t* size) {
    assert(buffer != nullptr);
    assert(size != nullptr);
    assert(*size > 0);
    const auto paths = HANDLE_AS_APP_CONTEXT(handle)->getPaths();
    const auto assets_path = paths->getAssetsPath();
    const auto expected_length = assets_path.length() + 1;
    if (*size < expected_length) {
        LOGGER.error("Path buffer not large enough ({} < {})", *size, expected_length);
        *size = 0;
        buffer[0] = 0;
        return;
    }

    strcpy(buffer, assets_path.c_str());
    *size = assets_path.length();
}

void tt_app_get_assets_child_path(AppHandle handle, const char* childPath, char* buffer, size_t* size) {
    assert(buffer != nullptr);
    assert(size != nullptr);
    assert(*size > 0);
    const auto paths = HANDLE_AS_APP_CONTEXT(handle)->getPaths();
    const auto resolved_path = paths->getAssetsPath(childPath);
    const auto resolved_path_length = resolved_path.length();
    if (*size < (resolved_path_length + 1)) {
        LOGGER.error("Path buffer not large enough ({} < {})", *size, (resolved_path_length + 1));
        *size = 0;
        buffer[0] = 0;
        return;
    }

    strcpy(buffer, resolved_path.c_str());
    *size = resolved_path_length;

}

}