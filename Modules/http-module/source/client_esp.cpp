// SPDX-License-Identifier: Apache-2.0
#include <http/private/client.h>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <sdkconfig.h>

#include <tactility/log.h>

#include <new>

namespace {

constexpr auto* TAG = "http-client";
// Holds one header line at a time, and has to fit long redirect targets
constexpr int RECEIVE_BUFFER_SIZE = 2048;
constexpr int TRANSMIT_BUFFER_SIZE = 2048;

class EspBackend final : public HttpClientBackend {
    esp_http_client_handle_t handle = nullptr;
    HttpClient* owner = nullptr;
    bool done = false;
    /** Whether the headers of the final response are complete, as interim (1xx) ones come before it */
    bool finalHead = false;

    static esp_err_t onEvent(esp_http_client_event_t* event) {
        auto* self = static_cast<EspBackend*>(event->user_data);
        // Chunked trailers are reported as headers too, and must not change the ones that the caller already has
        if (event->event_id == HTTP_EVENT_ON_HEADER && !self->finalHead) {
            self->owner->addResponseHeader(event->header_key, event->header_value);
        } else if (event->event_id == HTTP_EVENT_ON_HEADERS_COMPLETE) {
            self->finalHead = esp_http_client_get_status_code(event->client) >= 200;
            if (!self->finalHead) {
                self->owner->clearResponseHeaders();
            }
        }
        return ESP_OK;
    }

public:
    ~EspBackend() override {
        if (handle != nullptr) {
            esp_http_client_close(handle);
            esp_http_client_cleanup(handle);
        }
    }

    HttpClientFailure open(HttpClient& client, const std::string& url) override {
        owner = &client;
        esp_http_client_config_t config {};
        config.url = url.c_str();
        config.auth_type = HTTP_AUTH_TYPE_NONE;
        if (client.certificatePem != nullptr) {
            config.cert_pem = client.certificatePem;
        } else {
            config.crt_bundle_attach = esp_crt_bundle_attach;
        }
        config.tls_version = ESP_HTTP_CLIENT_TLS_VER_ANY;
        config.method = HTTP_METHOD_GET;
        config.timeout_ms = static_cast<int>(client.timeoutMs);
        config.addr_type = HTTP_ADDR_TYPE_INET;
        config.buffer_size = RECEIVE_BUFFER_SIZE;
        config.buffer_size_tx = TRANSMIT_BUFFER_SIZE;
        config.disable_auto_redirect = true;
        config.event_handler = onEvent;
        config.user_data = this;

        handle = esp_http_client_init(&config);
        if (handle == nullptr) {
            return HTTP_CLIENT_FAILURE_MEMORY;
        }
        for (const auto& header : client.requestHeaders) {
            if (esp_http_client_set_header(handle, header.first.c_str(), header.second.c_str()) != ESP_OK) {
                return HTTP_CLIENT_FAILURE_MEMORY;
            }
        }

        const esp_err_t result = esp_http_client_open(handle, 0);
        if (result != ESP_OK) {
            int tlsCode = 0;
            int tlsFlags = 0;
            esp_http_client_get_and_clear_last_tls_error(handle, &tlsCode, &tlsFlags);
            LOG_E(TAG, "Failed to connect: %s, TLS -0x%04x flags 0x%x", esp_err_to_name(result), static_cast<unsigned>(-tlsCode), static_cast<unsigned>(tlsFlags));
            return tlsFlags != 0 ? HTTP_CLIENT_FAILURE_CERTIFICATE : HTTP_CLIENT_FAILURE_CONNECT;
        }
        if (client.cancelled) {
            return HTTP_CLIENT_FAILURE_CANCELLED;
        }

        int64_t length = 0;
        while (!finalHead) {
            length = esp_http_client_fetch_headers(handle);
            if (length < 0) {
                return HTTP_CLIENT_FAILURE_PROTOCOL;
            }
            if (client.cancelled) {
                return HTTP_CLIENT_FAILURE_CANCELLED;
            }
        }
        client.statusCode = esp_http_client_get_status_code(handle);
        const bool hasLength = !esp_http_client_is_chunked_response(handle) && http_client_get_header(&client, "content-length") != nullptr;
        client.contentLength = hasLength ? length : -1;
        done = client.statusCode == 204 || client.statusCode == 304 || client.contentLength == 0;
        return HTTP_CLIENT_FAILURE_NONE;
    }

    int read(HttpClient& client, void* buffer, size_t size) override {
        if (done) {
            return 0;
        }
        const int result = esp_http_client_read(handle, static_cast<char*>(buffer), static_cast<int>(size));
        if (result > 0) {
            return result;
        }
        if (result < 0) {
            return -1;
        }
        // Without a length or chunks, the body ends when the server closes the connection
        const bool endsOnClose = client.contentLength < 0 && !esp_http_client_is_chunked_response(handle);
        if (endsOnClose || esp_http_client_is_complete_data_received(handle)) {
            done = true;
            return 0;
        }
        return -1;
    }
};

}

std::unique_ptr<HttpClientBackend> http_client_create_backend() {
    return std::unique_ptr<HttpClientBackend>(new (std::nothrow) EspBackend());
}
