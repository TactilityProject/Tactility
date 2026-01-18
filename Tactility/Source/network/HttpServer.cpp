#ifdef ESP_PLATFORM

#include <Tactility/network/HttpServer.h>

#include <Tactility/Logger.h>
#include <Tactility/service/wifi/Wifi.h>

namespace tt::network {

static const auto LOGGER = Logger("HttpServer");

static constexpr size_t INTERNAL_URI_HANDLER_COUNT = 2;

bool HttpServer::startInternal() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = stackSize;
    config.server_port = port;
    config.uri_match_fn = matchUri;
    config.max_uri_handlers = handlers.size() + INTERNAL_URI_HANDLER_COUNT;

    if (httpd_start(&server, &config) != ESP_OK) {
        LOGGER.error("Failed to start http server on port {}", port);
        return false;
    }

    bool allRegistered = true;
    for (std::vector<httpd_uri_t>::reference handler : handlers) {
        if (httpd_register_uri_handler(server, &handler) != ESP_OK) {
            LOGGER.error("Failed to register URI handler: {}", handler.uri);
            allRegistered = false;
        }
    }
    if (!allRegistered) {
        httpd_stop(server);
        server = nullptr;
        return false;
    }

    LOGGER.info("Started on port {}", config.server_port);
    return true;
}

void HttpServer::stopInternal() {
    LOGGER.info("Stopping server");
    if (server != nullptr) {
        if (httpd_stop(server) == ESP_OK) {
            server = nullptr;
        } else {
            LOGGER.warn("Error while stopping");
        }
    }
}

bool HttpServer::start() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (isStarted()) {
        LOGGER.warn("Already started");
        return false;
    }

    return startInternal();
}

void HttpServer::stop() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!isStarted()) {
        LOGGER.warn("Not started");
        return;
    }

    stopInternal();
}

}

#endif