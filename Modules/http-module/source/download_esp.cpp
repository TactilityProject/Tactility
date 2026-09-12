// SPDX-License-Identifier: Apache-2.0
#include <http/private/download.h>

#include <esp_heap_caps.h>
#include <esp_http_client.h>

#include <tactility/freertos/task.h>
#include <tactility/log.h>

namespace {

constexpr auto* TAG = "http-download";

// RAII: guarantees esp_http_client_close()/_cleanup() run on every exit path below.
class EspDownloadClient {
    esp_http_client_handle_t client = nullptr;
    bool isOpen = false;

public:
    ~EspDownloadClient() {
        if (isOpen) {
            esp_http_client_close(client);
        }
        if (client != nullptr) {
            esp_http_client_cleanup(client);
        }
    }

    bool init(const esp_http_client_config_t& config) {
        client = esp_http_client_init(&config);
        return client != nullptr;
    }

    bool open() {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            return false;
        }
        isOpen = true;
        return true;
    }

    bool fetchHeaders() const { return esp_http_client_fetch_headers(client) >= 0; }

    int getStatusCode() const { return esp_http_client_get_status_code(client); }

    int getContentLength() const { return esp_http_client_get_content_length(client); }

    int read(char* buffer, int size) const { return esp_http_client_read(client, buffer, size); }

    bool isComplete() const { return esp_http_client_is_complete_data_received(client); }
};

// esp_http_client_config_t::cert_pem needs a NUL-terminated buffer.
bool read_certificate(const std::string& certPath, std::string& outCertificate) {
    auto* file = fopen(certPath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return false;
    }

    outCertificate.resize(static_cast<size_t>(size));
    size_t read_bytes = fread(outCertificate.data(), 1, static_cast<size_t>(size), file);
    fclose(file);
    return read_bytes == static_cast<size_t>(size);
}

}

HttpDownloadEvent http_download_run(const std::string& url, const std::string& certPath, const std::string& targetPath, HttpDownloadLink* link) {
    if (http_download_is_cancelled(link)) {
        return http_download_make_cancelled_event();
    }

    std::string certificate;
    if (!read_certificate(certPath, certificate)) {
        return http_download_make_error_event("Failed to read certificate file");
    }

    esp_http_client_config_t config {};
    config.url = url.c_str();
    config.auth_type = HTTP_AUTH_TYPE_NONE;
    config.cert_pem = certificate.c_str();
    config.cert_len = certificate.size() + 1;
    config.tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 5000;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.addr_type = HTTP_ADDR_TYPE_INET;

    // Total free can look fine while a fragmented heap still can't satisfy one large-enough
    // allocation. Logging both makes a future ALLOC_FAILED here diagnosable from the log alone.
    LOG_I(TAG, "Free internal heap before connecting: %u bytes (largest block: %u bytes)",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));

    EspDownloadClient client;
    if (!client.init(config)) {
        return http_download_make_error_event("Failed to initialize HTTP client");
    }
    if (!client.open()) {
        return http_download_make_error_event("Failed to open connection");
    }
    if (!client.fetchHeaders()) {
        return http_download_make_error_event("Failed to fetch response headers");
    }

    auto status_code = client.getStatusCode();
    if (status_code < 200 || status_code >= 300) {
        return http_download_make_error_event("Server response is not OK", status_code);
    }

    if (http_download_is_cancelled(link)) {
        return http_download_make_cancelled_event(status_code);
    }

    // Downloaded to a sibling ".tmp" file first and only renamed onto targetPath on success, so a
    // failed/cancelled download (or a crash mid-write) never leaves a partial file at targetPath.
    auto tempPath = targetPath + ".tmp";
    auto* file = fopen(tempPath.c_str(), "wb");
    if (file == nullptr) {
        return http_download_make_error_event("Failed to open target file", status_code);
    }

    // -1 means the length is unknown (e.g. a chunked response). Read until the client reports
    // the body done instead of counting down a length that was never given.
    auto content_length = client.getContentLength();
    bool length_known = content_length >= 0;
    auto bytes_left = content_length;
    char buffer[512];
    while (!length_known || bytes_left > 0) {
        if (http_download_is_cancelled(link)) {
            fclose(file);
            remove(tempPath.c_str());
            return http_download_make_cancelled_event(status_code);
        }
        int data_read = client.read(buffer, sizeof(buffer));
        if (data_read < 0) {
            fclose(file);
            remove(tempPath.c_str());
            return http_download_make_error_event("Failed to read response data", status_code);
        }
        if (data_read == 0) {
            break;
        }
        if (length_known) {
            bytes_left -= data_read;
        }
        if (fwrite(buffer, 1, static_cast<size_t>(data_read), file) != static_cast<size_t>(data_read)) {
            fclose(file);
            remove(tempPath.c_str());
            return http_download_make_error_event("Failed to write downloaded data", status_code);
        }
        taskYIELD();
    }

    // Distinguishes a clean end (chunked terminator seen, or a known length fully read) from a
    // connection that just stopped producing data early.
    if (!client.isComplete()) {
        fclose(file);
        remove(tempPath.c_str());
        return http_download_make_error_event("Response body was incomplete", status_code);
    }

    if (fclose(file) != 0) {
        remove(tempPath.c_str());
        return http_download_make_error_event("Failed to finalize downloaded file", status_code);
    }

    // Some embedded filesystems (e.g. FATFS) reject rename() onto an existing path instead of
    // replacing it like POSIX does. Only clear the way if the plain rename actually needed it, so
    // targetPath is never removed unless the new file is confirmed ready to replace it.
    if (rename(tempPath.c_str(), targetPath.c_str()) != 0) {
        remove(targetPath.c_str());
        if (rename(tempPath.c_str(), targetPath.c_str()) != 0) {
            remove(tempPath.c_str());
            return http_download_make_error_event("Failed to finalize downloaded file", status_code);
        }
    }

    return http_download_make_success_event(status_code);
}
