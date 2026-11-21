// SPDX-License-Identifier: Apache-2.0
#include <http/private/download.h>
#include <http/private/http_parsing.h>

#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <tactility/log.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

constexpr auto* TAG = "http-download";
constexpr int TIMEOUT_SECONDS = 5;
constexpr size_t BUFFER_SIZE = 512;
constexpr const char* SYSTEM_CERTIFICATE_FILES[] = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/cert.pem",
};

// A write to a closed connection must fail instead of raising SIGPIPE. macOS has no MSG_NOSIGNAL, but SO_NOSIGPIPE on the socket.
#ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SEND_FLAGS = 0;
#endif

int bio_send(void* context, const unsigned char* data, size_t size) {
    const int fd = *static_cast<int*>(context);
    const ssize_t sent = send(fd, data, size, SEND_FLAGS);
    if (sent >= 0) {
        return static_cast<int>(sent);
    }
    if (errno == EINTR) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_TIMEOUT : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

int bio_receive(void* context, unsigned char* data, size_t size) {
    const int fd = *static_cast<int*>(context);
    const ssize_t received = recv(fd, data, size, 0);
    if (received >= 0) {
        return static_cast<int>(received);
    }
    if (errno == EINTR) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    // The socket's receive timeout elapsed
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_TIMEOUT : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

// RAII: the TLS session, the socket and the certificate are released on every exit path
class TlsConnection {
    int fd = -1;
    bool handshakeDone = false;
    mbedtls_x509_crt ca {};
    mbedtls_ssl_config config {};
    mbedtls_ssl_context ssl {};
    // Bytes that readLine() read beyond the line it returned
    std::string pending;

public:
    TlsConnection() {
        mbedtls_x509_crt_init(&ca);
        mbedtls_ssl_config_init(&config);
        mbedtls_ssl_init(&ssl);
    }

    ~TlsConnection() {
        if (handshakeDone) {
            mbedtls_ssl_close_notify(&ssl);
        }
        if (fd >= 0) {
            close(fd);
        }
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&config);
        mbedtls_x509_crt_free(&ca);
    }

    bool loadCertificate(const std::string& certPem) {
        if (certPem.empty()) {
            return loadSystemCertificates();
        }
        // A PEM certificate is parsed as a NUL-terminated string
        return mbedtls_x509_crt_parse(&ca, reinterpret_cast<const unsigned char*>(certPem.c_str()), certPem.size() + 1) == 0;
    }

    /** The certificates of the operating system, for servers that don't have a certificate of their own */
    bool loadSystemCertificates() {
        for (const char* path : SYSTEM_CERTIFICATE_FILES) {
            if (mbedtls_x509_crt_parse_file(&ca, path) >= 0) {
                return true;
            }
        }
        LOG_E(TAG, "No system certificates found");
        return false;
    }

    bool connectSocket(const HttpUrl& url) {
        addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* addresses = nullptr;
        if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &addresses) != 0) {
            LOG_E(TAG, "Failed to resolve %s", url.host.c_str());
            return false;
        }

        const timeval timeout { .tv_sec = TIMEOUT_SECONDS, .tv_usec = 0 };
        for (auto* address = addresses; address != nullptr; address = address->ai_next) {
            fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (fd < 0) {
                continue;
            }
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
            const int enabled = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
            int result;
            do {
                result = ::connect(fd, address->ai_addr, address->ai_addrlen);
            } while (result != 0 && errno == EINTR);
            if (result == 0) {
                break;
            }
            close(fd);
            fd = -1;
        }
        freeaddrinfo(addresses);
        return fd >= 0;
    }

    bool handshake(const HttpUrl& url, HttpDownloadLink* link) {
        if (mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            return false;
        }
        mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&config, &ca, nullptr);
        // Same as the ESP32 implementation
        mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
        if (mbedtls_ssl_setup(&ssl, &config) != 0 || mbedtls_ssl_set_hostname(&ssl, url.host.c_str()) != 0) {
            return false;
        }
        mbedtls_ssl_set_bio(&ssl, &fd, bio_send, bio_receive, nullptr);

        int result;
        while ((result = mbedtls_ssl_handshake(&ssl)) != 0) {
            if ((result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) || http_download_is_cancelled(link)) {
                LOG_E(TAG, "TLS handshake with %s failed: -0x%04x", url.host.c_str(), static_cast<unsigned>(-result));
                return false;
            }
        }
        handshakeDone = true;
        return true;
    }

    bool writeAll(const std::string& data) {
        size_t offset = 0;
        while (offset < data.size()) {
            const int written = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(data.data()) + offset, data.size() - offset);
            if (written > 0) {
                offset += static_cast<size_t>(written);
            } else if (written != MBEDTLS_ERR_SSL_WANT_READ && written != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return false;
            }
        }
        return true;
    }

    /** @return the number of bytes read, 0 when the server closed the connection, or -1 on an error */
    int read(char* buffer, size_t size) {
        if (!pending.empty()) {
            const size_t count = std::min(size, pending.size());
            memcpy(buffer, pending.data(), count);
            pending.erase(0, count);
            return static_cast<int>(count);
        }
        while (true) {
            const int result = mbedtls_ssl_read(&ssl, reinterpret_cast<unsigned char*>(buffer), size);
            if (result >= 0) {
                return result;
            }
            if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                return 0;
            }
            if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return -1;
            }
        }
    }

    /** Reads a line that ends with CRLF, without the CRLF */
    bool readLine(std::string& line) {
        constexpr size_t MAX_LINE_LENGTH = 8192;
        while (true) {
            const auto end = pending.find("\r\n");
            if (end != std::string::npos) {
                line = pending.substr(0, end);
                pending.erase(0, end + 2);
                return true;
            }
            if (pending.size() > MAX_LINE_LENGTH) {
                return false;
            }
            char buffer[BUFFER_SIZE];
            const int result = mbedtls_ssl_read(&ssl, reinterpret_cast<unsigned char*>(buffer), sizeof(buffer));
            if (result > 0) {
                pending.append(buffer, static_cast<size_t>(result));
            } else if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return false;
            }
        }
    }
};

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

    TlsConnection connection;
    if (!connection.loadCertificate(certPem)) {
        return http_download_make_error_event("Failed to parse certificate");
    }
    if (!connection.connectSocket(parsedUrl) || !connection.handshake(parsedUrl, link)) {
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
