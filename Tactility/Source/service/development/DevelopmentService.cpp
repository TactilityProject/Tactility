#include <app/install.h>
#include <app/manager.h>
#include <app/start.h>

#include <tactility/log.h>

#include <Tactility/DeprecatedPaths.h>
#include <Tactility/StringUtils.h>
#include <Tactility/file/File.h>
#include <Tactility/network/HttpServerReq.h>
#include <Tactility/network/HttpdReq.h>
#include <Tactility/network/Url.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/development/DevelopmentService.h>
#include <Tactility/service/development/DevelopmentSettings.h>

#include <cstring>
#include <format>
#include <iterator>
#include <sstream>

namespace tt::service::development {

extern const ServiceManifest manifest;

constexpr auto* TAG = "DevService";

DevelopmentService::DevelopmentService() {
    HttpServerRequestHandler handlers[] = {
        { .uri = "/info", .method = HTTP_METHOD_GET, .callback = handleGetInfo, .user_ctx = this },
        { .uri = "/app/run", .method = HTTP_METHOD_POST, .callback = handleAppRun, .user_ctx = this },
        { .uri = "/app/install", .method = HTTP_METHOD_PUT, .callback = handleAppInstall, .user_ctx = this },
        { .uri = "/app/uninstall", .method = HTTP_METHOD_PUT, .callback = handleAppUninstall, .user_ctx = this },
    };
    HttpServerConfig config {
        .port = 6666,
        .address = "0.0.0.0",
        .stack_size = 5120,
        .handlers = handlers,
        .handler_count = std::size(handlers),
    };
    httpServer = http_server_alloc(&config);
    if (httpServer == nullptr) {
        LOG_E(TAG, "Failed to allocate http server");
    }
}

DevelopmentService::~DevelopmentService() {
    http_server_free(httpServer);
}

bool DevelopmentService::onStart(ServiceContext& service) {
    std::stringstream stream;
    stream << "{";
#ifdef ESP_PLATFORM
    stream << "\"cpuFamily\":\"" << CONFIG_IDF_TARGET << "\", ";
#else
    stream << "\"cpuFamily\":\"" << CONFIG_TT_DEVICE_ID << "\", ";
#endif
    stream << "\"osVersion\":\"" << TT_VERSION << "\", ";
    stream << "\"protocolVersion\":\"1.0.0\"";
    stream << "}";
    deviceResponse = stream.str();

    setEnabled(shouldEnableOnBoot());

    return true;
}

void DevelopmentService::onStop(ServiceContext& service) {
    setEnabled(false);
}

// region Enable/disable

void DevelopmentService::setEnabled(bool enabled) {
    if (httpServer == nullptr) {
        return;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();

    if (enabled) {
        if (!http_server_is_started(httpServer)) {
            http_server_start(httpServer);
        }
    } else {
        if (http_server_is_started(httpServer)) {
            http_server_stop(httpServer);
        }
    }
}

bool DevelopmentService::isEnabled() const {
    if (httpServer == nullptr) {
        return false;
    }

    auto lock = mutex.asScopedLock();
    lock.lock();
    return http_server_is_started(httpServer);
}

// region endpoints

error_t DevelopmentService::handleGetInfo(HttpServerRequest* request, void* user_ctx) {
    LOG_I(TAG, "GET /device");

    auto* service = static_cast<DevelopmentService*>(user_ctx);
    http_server_request_set_content_type(request, "application/json");
    if (http_server_request_send_string(request, service->deviceResponse.c_str()) != ERROR_NONE) {
        LOG_W(TAG, "Failed to send response body");
        return ERROR_UNDEFINED;
    }

    LOG_I(TAG, "[200] /device");
    return ERROR_NONE;
}

error_t DevelopmentService::handleAppRun(HttpServerRequest* request, void*) {
    LOG_I(TAG, "POST /app/run");

    std::string query;
    if (!network::getQueryOrSendError(request, query)) {
        return ERROR_UNDEFINED;
    }

    auto parameters = network::parseUrlQuery(query);
    auto id_key_pos = parameters.find("id");
    if (id_key_pos == parameters.end()) {
        LOG_W(TAG, "[400] /app/run id not specified");
        http_server_request_send_error(request, 400, "id not specified");
        return ERROR_UNDEFINED;
    }

    char app_id[32];
    AppInstanceId instance_id;
    // Warning: possible app closure between getting app id and instance id
    if (
        app_manager_get_topmost_app_id(app_id, sizeof(app_id)) == ERROR_NONE &&
        app_manager_get_topmost_instance_id(&instance_id) == ERROR_NONE
    ) {
        if (strcmp(id_key_pos->second.c_str(), app_id) == 0) {
            app_manager_stop(instance_id);
        }
    }

    AppStartContext context;
    if (app_start_context_from_id(id_key_pos->second.c_str(), &context) == ERROR_NONE) {
        app_start_with_context(&context, &instance_id);
    }

    LOG_I(TAG, "[200] /app/run %s", id_key_pos->second.c_str());
    http_server_request_send(request, nullptr, 0);

    return ERROR_NONE;
}

error_t DevelopmentService::handleAppInstall(HttpServerRequest* request, void*) {
    LOG_I(TAG, "PUT /app/install");

    std::string boundary;
    if (!network::getMultiPartBoundaryOrSendError(request, boundary)) {
        return ERROR_UNDEFINED;
    }

    size_t content_left = http_server_request_get_content_length(request);

    // Skip newline after reading boundary
    auto content_headers_data = network::receiveTextUntil(request, "\r\n\r\n");
    if (content_headers_data.empty()) {
        http_server_request_send_error(request, 400, "Multipart form error: preamble too long or unterminated");
        return ERROR_UNDEFINED;
    }
    content_left -= content_headers_data.length();
    auto content_header_lines = string::split(content_headers_data, "\r\n");
    std::vector<std::string> content_headers;
    for (auto& line : content_header_lines) {
        if (!line.empty()) {
            content_headers.push_back(line);
        }
    }

    auto content_disposition_map = network::parseContentDisposition(content_headers);
    if (content_disposition_map.empty()) {
        http_server_request_send_error(request, 400, "Multipart form error: invalid content disposition");
        return ERROR_UNDEFINED;
    }

    auto name_entry = content_disposition_map.find("name");
    auto filename_entry = content_disposition_map.find("filename");
    if (
        name_entry == content_disposition_map.end() ||
        filename_entry == content_disposition_map.end() ||
        name_entry->second != "app"
    ) {
        http_server_request_send_error(request, 400, "Multipart form error: name or filename parameter missing or mismatching");
        return ERROR_UNDEFINED;
    }

    // Receive boundary
    auto boundary_and_newlines_after_file = std::format("\r\n--{}--\r\n", boundary);
    auto file_size = content_left - boundary_and_newlines_after_file.length();

    // Create tmp directory
    const std::string tmp_path = getTempPath();
    if (!file::findOrCreateDirectory(tmp_path, 0777)) {
        http_server_request_send_error(request, 500, "Failed to create temp path");
        return ERROR_UNDEFINED;
    }

    std::string safe_name = file::getLastPathSegment(filename_entry->second);
    if (safe_name.empty() || safe_name.find("..") != std::string::npos ||
        safe_name.find('/') != std::string::npos || safe_name.find('\\') != std::string::npos) {
        http_server_request_send_error(request, 400, "invalid filename");
        return ERROR_UNDEFINED;
    }
    auto file_path = std::format("{}/{}", tmp_path, safe_name);
    if (network::receiveFile(request, file_size, file_path) != file_size) {
        file::deleteFile(file_path);
        http_server_request_send_error(request, 500, "Failed to receive file");
        return ERROR_UNDEFINED;
    }

    content_left -= file_size;

    // Read and verify part
    if (!network::readAndDiscardOrSendError(request, boundary_and_newlines_after_file)) {
        return ERROR_UNDEFINED;
    }
    content_left -= boundary_and_newlines_after_file.length();

    if (content_left != 0) {
        LOG_W(TAG, "We have more bytes at the end of the request parsing?!");
    }

    if (app_install(file_path.c_str()) != ERROR_NONE) {
        http_server_request_send_error(request, 500, "Failed to install");
        return ERROR_UNDEFINED;
    }

    if (!file::deleteFile(file_path)) {
        LOG_W(TAG, "Failed to delete %s", file_path.c_str());
    }

    LOG_I(TAG, "[200] /app/install -> %s", file_path.c_str());

    http_server_request_send(request, nullptr, 0);

    return ERROR_NONE;
}

error_t DevelopmentService::handleAppUninstall(HttpServerRequest* request, void*) {
    LOG_I(TAG, "PUT /app/uninstall");

    std::string query;
    if (!network::getQueryOrSendError(request, query)) {
        return ERROR_UNDEFINED;
    }

    auto parameters = network::parseUrlQuery(query);
    auto id_key_pos = parameters.find("id");
    if (id_key_pos == parameters.end()) {
        LOG_W(TAG, "[400] /app/uninstall id not specified");
        http_server_request_send_error(request, 400, "id not specified");
        return ERROR_UNDEFINED;
    }

    AppManifest manifest;
    if (app_manager_find_manifest(id_key_pos->second.c_str(), &manifest) != ERROR_NONE) {
        LOG_I(TAG, "[200] /app/uninstall %s (app wasn't installed)", id_key_pos->second.c_str());
        http_server_request_send(request, nullptr, 0);
        return ERROR_NONE;
    }

    if (app_uninstall(id_key_pos->second.c_str()) == ERROR_NONE) {
        LOG_I(TAG, "[200] /app/uninstall %s", id_key_pos->second.c_str());
        http_server_request_send(request, nullptr, 0);
        return ERROR_NONE;
    } else {
        LOG_W(TAG, "[500] /app/uninstall %s", id_key_pos->second.c_str());
        http_server_request_send_error(request, 500, "Failed to uninstall");
        return ERROR_UNDEFINED;
    }
}

// endregion

std::shared_ptr<DevelopmentService> findService() {
    return std::static_pointer_cast<DevelopmentService>(
        findServiceById(manifest.id)
    );
}

extern const ServiceManifest manifest = {
    .id = "tactility.development",
    .createService = create<DevelopmentService>
};

}
