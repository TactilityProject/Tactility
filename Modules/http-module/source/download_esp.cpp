// SPDX-License-Identifier: Apache-2.0
#include <http/private/download.h>

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <sdkconfig.h>

#include <tactility/freertos/task.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <memory>

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

}

HttpDownloadEvent http_download_run(const std::string& url, const std::string& certPem, const std::string& targetPath, HttpDownloadLink* link) {
    if (http_download_is_cancelled(link)) {
        return http_download_make_cancelled_event();
    }

    esp_http_client_config_t config {};
    config.url = url.c_str();
    config.auth_type = HTTP_AUTH_TYPE_NONE;
    if (certPem.empty()) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
        config.cert_pem = certPem.c_str();
        config.cert_len = certPem.size() + 1;
    }
    config.tls_version = ESP_HTTP_CLIENT_TLS_VER_ANY;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
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
#ifdef CONFIG_SPIRAM
    constexpr size_t buffer_size = 16384;
#else
    constexpr size_t buffer_size = 512;
#endif
    constexpr MemoryPolicy buffer_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    std::unique_ptr<char, decltype(&memory_free)> buffer_holder(static_cast<char*>(memory_alloc_with_policy(buffer_size, &buffer_policy)), &memory_free);
    if (buffer_holder == nullptr) {
        fclose(file);
        remove(tempPath.c_str());
        return http_download_make_error_event("Failed to allocate download buffer", status_code);
    }
    auto* buffer = buffer_holder.get();
    // Reads are small so cancellation is seen quickly, and written to the file in batches
    constexpr size_t read_size = 1024;
    size_t buffered = 0;
    bool body_done = false;
    while (!body_done) {
        if (http_download_is_cancelled(link)) {
            fclose(file);
            remove(tempPath.c_str());
            return http_download_make_cancelled_event(status_code);
        }
        if (length_known && bytes_left <= 0) {
            body_done = true;
        } else {
            int data_read = client.read(buffer + buffered, read_size < buffer_size - buffered ? read_size : buffer_size - buffered);
            if (data_read < 0) {
                fclose(file);
                remove(tempPath.c_str());
                return http_download_make_error_event("Failed to read response data", status_code);
            }
            if (data_read == 0) {
                body_done = true;
            } else {
                buffered += static_cast<size_t>(data_read);
                if (length_known) {
                    bytes_left -= data_read;
                }
            }
        }
        if (buffered > 0 && (body_done || buffered >= buffer_size)) {
            if (fwrite(buffer, 1, buffered, file) != buffered) {
                fclose(file);
                remove(tempPath.c_str());
                return http_download_make_error_event("Failed to write downloaded data", status_code);
            }
            buffered = 0;
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
