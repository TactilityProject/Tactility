// SPDX-License-Identifier: Apache-2.0
#include <http/private/download.h>
#include <http/private/http_parsing.h>
#include <http/private/posix_connection.h>

#include <psa/crypto.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

constexpr uint32_t TIMEOUT_MS = 5000;
constexpr size_t BUFFER_SIZE = 512;

// RAII: a file that's removed unless commit() renamed it onto the target path
class TempFile {
    std::string tempPath;
    FILE* file = nullptr;

public:
    explicit TempFile(const std::string& targetPath) : tempPath(targetPath + ".tmp") {
        file = fopen(tempPath.c_str(), "wb");
    }

    ~TempFile() {
        if (file != nullptr) {
            fclose(file);
            remove(tempPath.c_str());
        }
    }

    bool isOpen() const { return file != nullptr; }

    bool write(const char* data, size_t size) { return fwrite(data, 1, size, file) == size; }

    /** Closes the file and replaces the target path with it */
    bool commit(const std::string& targetPath) {
        const bool closed = fclose(file) == 0;
        file = nullptr;
        if (!closed || rename(tempPath.c_str(), targetPath.c_str()) != 0) {
            remove(tempPath.c_str());
            return false;
        }
        return true;
    }
};

}

HttpDownloadEvent http_download_run(const std::string& url, const std::string& certPem, const std::string& targetPath, HttpDownloadLink* link) {
    if (http_download_is_cancelled(link)) {
        return http_download_make_cancelled_event();
    }

    HttpUrl parsedUrl;
    if (!http_parse_https_url(url, parsedUrl)) {
        return http_download_make_error_event("Unsupported URL");
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        return http_download_make_error_event("Failed to initialize crypto");
    }
    HttpPosixConnection connection;
    if (!connection.loadCertificate(certPem)) {
        return http_download_make_error_event("Failed to parse certificate");
    }
    if (!connection.connectSocket(parsedUrl, TIMEOUT_MS) || !connection.handshake(parsedUrl, [link] { return http_download_is_cancelled(link); })) {
        return http_download_make_error_event("Failed to open connection");
    }

    const std::string request =
        "GET " + parsedUrl.path + " HTTP/1.1\r\n"
        "Host: " + parsedUrl.host + "\r\n"
        "User-Agent: Tactility\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n"
        "\r\n";
    if (!connection.writeAll(request)) {
        return http_download_make_error_event("Failed to send request");
    }

    std::string line;
    int statusCode = 0;
    if (!connection.readLine(line) || !http_parse_status_line(line, statusCode)) {
        return http_download_make_error_event("Failed to fetch response headers");
    }

    // -1 means the length is unknown: the body is chunked or ends when the server closes the connection
    long contentLength = -1;
    std::string contentLengthValue;
    bool hasContentLength = false;
    bool hasConflictingContentLengths = false;
    bool chunked = false;
    while (true) {
        if (!connection.readLine(line)) {
            return http_download_make_error_event("Failed to fetch response headers", statusCode);
        }
        if (line.empty()) {
            break;
        }
        std::string name;
        std::string value;
        if (!http_parse_header_line(line, name, value)) {
            continue;
        }
        if (name == "content-length") {
            // Repeated headers must have the same value, or the body's end is ambiguous
            if (hasContentLength && value != contentLengthValue) {
                hasConflictingContentLengths = true;
            }
            contentLengthValue = value;
            hasContentLength = true;
        } else if (name == "transfer-encoding" && value.find("chunked") != std::string::npos) {
            chunked = true;
        }
    }

    if (statusCode < 200 || statusCode >= 300) {
        return http_download_make_error_event("Server response is not OK", statusCode);
    }
    // A chunked body ignores the Content-Length
    if (hasContentLength && !chunked && (hasConflictingContentLengths || !http_parse_content_length(contentLengthValue, contentLength))) {
        return http_download_make_error_event("Invalid Content-Length", statusCode);
    }
    if (http_download_is_cancelled(link)) {
        return http_download_make_cancelled_event(statusCode);
    }

    // Downloaded to a sibling ".tmp" file first and only renamed onto targetPath on success, so a
    // failed/cancelled download never leaves a partial file at targetPath.
    TempFile file(targetPath);
    if (!file.isOpen()) {
        return http_download_make_error_event("Failed to open target file", statusCode);
    }

    char buffer[BUFFER_SIZE];
    bool cancelled = false;
    // Copies up to `count` body bytes to the file. A count of -1 copies until the server closes the connection.
    // @return an error message, or nullptr when the bytes were copied or the download was cancelled
    auto copyBody = [&](long count) -> const char* {
        while (count != 0) {
            if (http_download_is_cancelled(link)) {
                cancelled = true;
                return nullptr;
            }
            const size_t wanted = (count < 0) ? sizeof(buffer) : std::min(sizeof(buffer), static_cast<size_t>(count));
            const int received = connection.read(buffer, wanted);
            if (received < 0) {
                return "Failed to read response data";
            }
            if (received == 0) {
                return (count < 0) ? nullptr : "Response body was incomplete";
            }
            if (!file.write(buffer, static_cast<size_t>(received))) {
                return "Failed to write downloaded data";
            }
            if (count > 0) {
                count -= received;
            }
        }
        return nullptr;
    };

    const char* error = nullptr;
    if (chunked) {
        while (error == nullptr && !cancelled) {
            size_t chunkSize = 0;
            if (!connection.readLine(line) || !http_parse_chunk_size(line, chunkSize)) {
                error = "Failed to read response data";
            } else if (chunkSize == 0) {
                // The trailer ends with an empty line
                while (connection.readLine(line) && !line.empty()) {}
                break;
            } else if ((error = copyBody(static_cast<long>(chunkSize))) == nullptr && (!connection.readLine(line) || !line.empty())) {
                error = "Failed to read response data";
            }
        }
    } else {
        error = copyBody(contentLength);
    }

    if (cancelled) {
        return http_download_make_cancelled_event(statusCode);
    }
    if (error != nullptr) {
        return http_download_make_error_event(error, statusCode);
    }
    if (!file.commit(targetPath)) {
        return http_download_make_error_event("Failed to finalize downloaded file", statusCode);
    }
    return http_download_make_success_event(statusCode);
}
