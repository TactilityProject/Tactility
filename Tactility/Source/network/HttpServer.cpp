#ifdef ESP_PLATFORM

#include <Tactility/network/HttpServer.h>

#include <Tactility/service/wifi/Wifi.h>

#include <tactility/log.h>

namespace tt::network {

constexpr auto* TAG = "HttpServer";

static constexpr size_t INTERNAL_URI_HANDLER_COUNT = 2;

bool HttpServer::startInternal() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = stackSize;
    config.server_port = port;
    config.uri_match_fn = matchUri;
    config.max_uri_handlers = handlers.size() + INTERNAL_URI_HANDLER_COUNT;

    if (httpd_start(&server, &config) != ESP_OK) {
        LOG_E(TAG, "Failed to start http server on port %u", (unsigned)port);
        return false;
    }

    bool allRegistered = true;
    for (std::vector<httpd_uri_t>::reference handler : handlers) {
        if (httpd_register_uri_handler(server, &handler) != ESP_OK) {
            LOG_E(TAG, "Failed to register URI handler: %s", handler.uri);
            allRegistered = false;
        }
    }
    if (!allRegistered) {
        httpd_stop(server);
        server = nullptr;
        return false;
    }

    LOG_I(TAG, "Started on port %u", (unsigned)config.server_port);
    return true;
}

void HttpServer::stopInternal() {
    LOG_I(TAG, "Stopping server");
    if (server != nullptr) {
        if (httpd_stop(server) == ESP_OK) {
            server = nullptr;
        } else {
            LOG_W(TAG, "Error while stopping");
        }
    }
}

bool HttpServer::start() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (isStarted()) {
        LOG_W(TAG, "Already started");
        return true;
    }

    return startInternal();
}

void HttpServer::stop() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!isStarted()) {
        LOG_W(TAG, "Not started");
        return;
    }

    stopInternal();
}

}

#endif