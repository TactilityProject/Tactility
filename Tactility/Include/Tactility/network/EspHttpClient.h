#pragma once

#ifdef ESP_PLATFORM

#include <esp_http_client.h>
#include <tactility/log.h>

namespace tt::network {

class EspHttpClient {

    static constexpr auto* TAG = "EspHttpClient";

    std::unique_ptr<esp_http_client_config_t> config = nullptr;
    esp_http_client_handle_t client = nullptr;
    bool isOpen = false;

public:

    ~EspHttpClient() {
        if (isOpen) {
            esp_http_client_close(client);
        }

        if (client != nullptr) {
            esp_http_client_cleanup(client);
        }
    }

    bool init(std::unique_ptr<esp_http_client_config_t> inConfig) {
        LOG_I(TAG, "init(%s)", inConfig->url);
        assert(this->config == nullptr);
        config = std::move(inConfig);
        client = esp_http_client_init(config.get());
        return client != nullptr;
    }

    bool open() {
        assert(client != nullptr);
        LOG_I(TAG, "open()");
        auto result = esp_http_client_open(client, 0);

        if (result != ESP_OK) {
            LOG_E(TAG, "open() failed: %s", esp_err_to_name(result));
            return false;
        }

        isOpen = true;
        return true;
    }

    bool fetchHeaders() const {
        assert(client != nullptr);
        LOG_I(TAG, "fetchHeaders()");
        return esp_http_client_fetch_headers(client) >= 0;
    }

    bool isStatusCodeOk() const {
        assert(client != nullptr);
        const auto status_code = getStatusCode();
        return status_code >= 200 && status_code < 300;
    }

    int getStatusCode() const {
        assert(client != nullptr);
        const auto status_code = esp_http_client_get_status_code(client);
        LOG_I(TAG, "Status code %d", status_code);
        return status_code;
    }

    int getContentLength() const {
        assert(client != nullptr);
        return esp_http_client_get_content_length(client);
    }

    int read(char* bytes, int size) const {
        assert(client != nullptr);
        LOG_I(TAG, "read(%d)", size);
        return esp_http_client_read(client, bytes, size);
    }

    int readResponse(char* bytes, int size) const {
        assert(client != nullptr);
        LOG_I(TAG, "readResponse(%d)", size);
        return esp_http_client_read_response(client, bytes, size);
    }

    bool close() {
        assert(client != nullptr);
        LOG_I(TAG, "close()");
        if (esp_http_client_close(client) == ESP_OK) {
            isOpen = false;
        }
        return !isOpen;
    }

    bool cleanup() {
        assert(client != nullptr);
        assert(!isOpen);
        LOG_I(TAG, "cleanup()");
        const auto result = esp_http_client_cleanup(client);
        client = nullptr;
        return result == ESP_OK;
    }
};

}

#endif