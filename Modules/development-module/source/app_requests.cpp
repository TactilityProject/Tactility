// SPDX-License-Identifier: Apache-2.0
#include <development/requests.h>

#include <app/install.h>
#include <app/manager.h>
#include <app/start.h>

#include <tactility/filesystem/fs.h>
#include <tactility/log.h>

#include <sys/stat.h>

#include <cstring>

namespace {

constexpr auto* TAG = "DevelopmentApp";

/** The file name only names the temporary file, so it must not point elsewhere */
bool isSafeFileName(const char* name) {
    return strstr(name, "..") == nullptr && strchr(name, '/') == nullptr && strchr(name, '\\') == nullptr;
}

const char* installReceivedPackage(DevelopmentRequestContext& /*context*/, const char* path) {
    // The tar parser aborts the firmware on archives that aren't made of 512-byte blocks
    struct stat info {};
    if (stat(path, &info) != 0 || info.st_size == 0 || info.st_size % 512 != 0) {
        return "invalid package";
    }
    const error_t error = app_install(path);
    if (error == ERROR_NONE) {
        LOG_I(TAG, "Installed %s", path);
        return nullptr;
    }
    LOG_W(TAG, "Failed to install %s: %d", path, error);
    return error == ERROR_INVALID_ARGUMENT ? "invalid package" : "failed to install";
}

void handleInstall(DevelopmentRequestContext& context, const DevelopmentFrame& frame) {
    char name[FILE_MAX_PATH_STRING_LENGTH];
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!development_requests_read_string(frame, name, sizeof(name)) || !isSafeFileName(name)) {
        development_requests_start_push(context, nullptr, "invalid file name", nullptr, true);
        return;
    }
    if (paths_get_temp_path(directory, sizeof(directory)) != ERROR_NONE) {
        development_requests_start_push(context, nullptr, "no temporary directory", nullptr, true);
        return;
    }
    const int written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(path)) {
        development_requests_start_push(context, nullptr, "invalid file name", nullptr, true);
        return;
    }
    development_requests_start_push(context, path, nullptr, installReceivedPackage, true);
}

bool handleUninstall(DevelopmentRequestContext& context, const DevelopmentFrame& frame) {
    AppId id;
    if (!development_requests_read_string(frame, id, sizeof(id))) {
        return development_requests_send_fail(context, "invalid id");
    }
    PackageSummary package;
    if (app_manager_find_package(id, &package) != ERROR_NONE) {
        LOG_I(TAG, "Uninstall %s: not installed", id);
        return development_requests_send_ok(context);
    }
    if (app_uninstall(id) != ERROR_NONE) {
        LOG_W(TAG, "Failed to uninstall %s", id);
        return development_requests_send_fail(context, "failed to uninstall");
    }
    LOG_I(TAG, "Uninstalled %s", id);
    return development_requests_send_ok(context);
}

bool handleRun(DevelopmentRequestContext& context, const DevelopmentFrame& frame) {
    AppId id;
    if (!development_requests_read_string(frame, id, sizeof(id))) {
        return development_requests_send_fail(context, "invalid id");
    }
    AppStartContext startContext;
    if (app_start_context_from_id(id, &startContext) != ERROR_NONE) {
        return development_requests_send_fail(context, "not found");
    }

    // Restarts the app when it's already in the foreground. It might close between both calls.
    AppId topmostId;
    AppInstanceId instanceId;
    if (app_manager_get_topmost_app_id(topmostId, sizeof(topmostId)) == ERROR_NONE &&
        app_manager_get_topmost_instance_id(&instanceId) == ERROR_NONE &&
        strcmp(topmostId, id) == 0) {
        app_manager_stop(instanceId);
    }

    if (app_start_with_context(&startContext, &instanceId) != ERROR_NONE) {
        return development_requests_send_fail(context, "failed to start");
    }
    LOG_I(TAG, "Started %s", id);
    return development_requests_send_ok(context);
}

} // namespace

bool development_app_handle(DevelopmentRequestContext& context, const DevelopmentFrame& frame) {
    switch (frame.type) {
        case DEVELOPMENT_FRAME_APP_INSTALL:
            handleInstall(context, frame);
            return true;
        case DEVELOPMENT_FRAME_APP_UNINSTALL:
            return handleUninstall(context, frame);
        case DEVELOPMENT_FRAME_APP_RUN:
            return handleRun(context, frame);
        default:
            return false;
    }
}
